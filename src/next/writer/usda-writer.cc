// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Writer Implementation

#include "usda-writer.hh"
#include "value-printer.hh"
#include "stream-writer.hh"
#include "dtoa.hh"
#include "../strfmt.hh"
#include "../layer/property-index.hh"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <atomic>
#include <thread>
#endif

namespace tinyusdz {
namespace next {

namespace {

// One unit of parallel write work, emitted in document order by a single serial
// build pass. The main thread produces cheap structural glue as Text; every array
// value worth offloading becomes a WholeValue task (formatted by one worker) or,
// for a large chunkable array, a run of Chunk tasks (formatted by many workers).
struct WriteTask {
  enum class Kind : uint8_t { Text, WholeValue, Chunk } kind;
  std::string text;             // Text: literal bytes, produced during the walk
  const Value* value = nullptr; // WholeValue/Chunk: borrowed (Layer or holder)
  size_t lo = 0;                // Chunk: USD element range [lo, hi)
  size_t hi = 0;
  bool open = false;            // Chunk: emit leading '['
  bool close = false;           // Chunk: emit trailing ']'
  int free_idx = -1;            // after consuming, free holder[free_idx] (or -1)
};

// Offload tuning. An array property with >= kOffloadMinElems USD elements is
// formatted by a worker rather than inline on the build thread (tiny arrays stay
// inline to avoid task spam). A directly chunkable array with >= kSplitMinElems
// elements is split into kChunkElems-sized pieces so one giant array spreads
// across workers; kChunkElems also bounds each worker buffer (~3 MB for float3 at
// 64Ki). A giant chunkable-TYPE array that is not borrowable (compressed/lazy) is
// decoded once into an owned buffer and then chunked, but only past the larger
// kMaterializeChunkMin so the one-off decode pays off versus a single WholeValue.
constexpr size_t kChunkElems = 64u * 1024u;
constexpr size_t kSplitMinElems = 2u * kChunkElems;
constexpr size_t kOffloadMinElems = 256u;
constexpr size_t kMaterializeChunkMin = 1u << 20;  // 1Mi elements

// Collects ordered tasks during the serial build walk. Structural bytes accrue in
// `cur` (the StreamWriter target); each offloaded array flushes `cur` as a Text
// task and appends WholeValue/Chunk task(s). The worker emits the array's full
// `[...]`, so the build path writes nothing for the value itself. Giant
// non-borrowable arrays are materialized into `holder` (stable addresses) so their
// chunks can alias one decoded buffer; the buffer is freed once consumed.
struct SegmentSink {
  std::vector<WriteTask>* tasks = nullptr;
  std::string* cur = nullptr;
  const PrintOptions* po = nullptr;
  std::deque<Value>* holder = nullptr;

  void flush_text() {
    if (cur && !cur->empty()) {
      WriteTask t;
      t.kind = WriteTask::Kind::Text;
      t.text = std::move(*cur);
      cur->clear();
      tasks->push_back(std::move(t));
    }
  }

  void push_chunks(const Value* src, size_t n, int free_idx) {
    for (size_t lo = 0; lo < n; lo += kChunkElems) {
      WriteTask t;
      t.kind = WriteTask::Kind::Chunk;
      t.value = src;
      t.lo = lo;
      t.hi = (lo + kChunkElems < n) ? (lo + kChunkElems) : n;
      t.open = (lo == 0);
      t.close = (t.hi == n);
      if (t.close) t.free_idx = free_idx;  // free the decoded buffer last
      tasks->push_back(std::move(t));
    }
  }

  void offload_array(const Value* v) {
    flush_text();
    const size_t n = ArrayElementCount(*v);
    if (n >= kSplitMinElems && IsChunkableArray(*v, *po)) {
      push_chunks(v, n, /*free_idx=*/-1);  // borrowable: chunk in place
    } else if (n >= kMaterializeChunkMin && holder && IsChunkableType(*v, *po)) {
      holder->push_back(v->materialized_copy());  // decode once, then chunk
      push_chunks(&holder->back(), n,
                  static_cast<int>(holder->size() - 1));
    } else {
      WriteTask t;
      t.kind = WriteTask::Kind::WholeValue;
      t.value = v;
      tasks->push_back(std::move(t));
    }
  }
};

// Freestanding integer -> decimal string: IntToStr/UIntToStr live in ../strfmt.hh
// (shared with the value printer).

// Get specifier keyword
const char* SpecifierKeyword(PrimSpecifier spec) {
  switch (spec) {
    case PrimSpecifier::Def:   return "def";
    case PrimSpecifier::Over:  return "over";
    case PrimSpecifier::Class: return "class";
    default: return "def";
  }
}

// Write indent.
//
// Emits `depth` copies of `indent` in a single write, backed by a thread-local
// cache of the repeated unit (grown on demand). This avoids `depth` tiny writes
// per prim/property on the hot path. The cache is thread_local so it stays
// correct under the parallel subtree stitcher (each worker has its own
// StreamWriter). Byte-identical to emitting `indent` in a loop for any unit.
void WriteIndent(StreamWriter& os, int depth, const std::string& indent) {
  if (depth <= 0 || indent.empty()) return;
  thread_local std::string pad;    // cached repetition of `unit`
  thread_local std::string unit;   // the indent unit `pad` was built from
  thread_local int levels = 0;     // number of `unit` copies currently in `pad`
  if (unit != indent) {
    unit = indent;
    pad.clear();
    levels = 0;
  }
  if (depth > levels) {
    pad.reserve(static_cast<size_t>(depth) * indent.size());
    for (; levels < depth; ++levels) pad += indent;
  }
  os.write(pad.data(), static_cast<size_t>(depth) * indent.size());
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
void WriteLayerMeta(StreamWriter& os, const LayerMeta& meta,
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
    lines.push_back(opts.indent + "metersPerUnit = " + format_g(meta.metersPerUnit, opts.double_precision));
  }

  if (meta.startTimeCode != 0.0) {
    lines.push_back(opts.indent + "startTimeCode = " + format_g(meta.startTimeCode, opts.double_precision));
  }

  if (meta.endTimeCode != 0.0) {
    lines.push_back(opts.indent + "endTimeCode = " + format_g(meta.endTimeCode, opts.double_precision));
  }

  if (meta.timeCodesPerSecond != 24.0) {
    lines.push_back(opts.indent + "timeCodesPerSecond = " + format_g(meta.timeCodesPerSecond, opts.double_precision));
  }

  if (meta.upAxis != "Y") {
    lines.push_back(opts.indent + "upAxis = " + EscapeString(meta.upAxis));
  }

  if (meta.framesPerSecond_set) {
    lines.push_back(opts.indent + "framesPerSecond = " + format_g(meta.framesPerSecond, opts.double_precision));
  }

  if (meta.kilogramsPerUnit_set) {
    lines.push_back(opts.indent + "kilogramsPerUnit = " + format_g(meta.kilogramsPerUnit, opts.double_precision));
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
    std::string s = opts.indent + "subLayers = [\n";
    for (const auto& layer : meta.subLayers) {
      s += opts.indent + opts.indent + "@" + layer + "@,\n";
    }
    s += opts.indent + "]";
    lines.push_back(std::move(s));
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
bool WritePropMeta(StreamWriter& os, const PrimSpec& spec, PropNameId name_id,
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
    kv("elementSize = " + IntToStr(static_cast<long long>(m->elementSize)));
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
    kv("weight = " + format_g(m->weight, opts.double_precision));
  }
  if (m->authored & PropMeta::kUnauthoredIdx)
    kv("unauthoredValuesIndex = " + IntToStr(static_cast<long long>(m->unauthoredValuesIndex)));
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
void WriteTimeSamples(StreamWriter& os, const std::string& name, PropNameId name_id,
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
    os << format_g(sample.first, opts.double_precision) << ": ";

    const Value* val = spec.time_sample_value(sample.second);
    if (val && val->is_block()) {
      os << "None";  // a blocked sample (`123: None`)
    } else if (val) {
      // Stream large arrays directly; lazy crate-backed POD arrays can be
      // borrowed without decoding, while unsupported encodings fall back to a
      // transient materialized value inside the value printer.
      PrintValue(os, *val, print_opts);
    } else {
      os << "None";
    }
    os << ",\n";
  }

  WriteIndent(os, depth, opts.indent);
  os << "}";  // caller appends optional metadata block + newline
}

// Write a property value
void WriteProperty(StreamWriter& os, const PropSlot& slot, const PrimSpec& spec,
                   int depth, const USDAWriteOptions& opts,
                   SegmentSink* segsink = nullptr) {
  PropNameTable& name_table = GetPropNameTable();
  const std::string& name = name_table.get(slot.name_id);

  // Check if this property has time samples
  if (slot.is_time_sampled() && spec.has_time_samples(slot.name_id)) {
    // USDA forbids metadata after a `.timeSamples` block. If the attribute has
    // authored metadata, emit it on a bare declaration line FIRST (usdcat form):
    //   <type> <name> ( ...meta... )
    //   <type> <name>.timeSamples = { ... }
    const PropMeta* pm = spec.property_meta(slot.name_id);
    if (pm && !pm->empty()) {
      WriteIndent(os, depth, opts.indent);
      if (opts.emit_custom && slot.is_custom()) os << "custom ";
      if (slot.is_uniform()) os << "uniform ";
      if (const std::string* decl = spec.property_type_name(name)) {
        os << *decl;
      } else {
        const char* tn = GetTypeName(static_cast<TypeId>(slot.value_type));
        os << (tn ? tn : "token");
        if (slot.is_array()) os << "[]";
      }
      os << " " << name;
      WritePropMeta(os, spec, slot.name_id, depth, opts);
      os << "\n";
    }
    WriteTimeSamples(os, name, slot.name_id, spec, depth, opts);
    os << "\n";
    return;
  }

  // Type name. Prefer the declared name (round-trips string[] vs token[],
  // role types, and value-less / connection-only attrs whose stored value type
  // is Invalid). GetTypeName returns nullptr for Invalid, so guard the fallback.
  TypeId type_id = static_cast<TypeId>(slot.value_type);
  std::string type_name;
  if (const std::string* decl = spec.property_type_name(name)) {
    type_name = *decl;
  } else {
    const char* tn = GetTypeName(type_id);
    type_name = tn ? tn : "token";
    if (slot.is_array()) type_name += "[]";
  }

  // Emit the leading `<indent><qualifiers><type> <name>` shared by the value
  // line and the `.connect` line (USDA repeats the type on each statement).
  auto emit_decl = [&]() {
    WriteIndent(os, depth, opts.indent);
    // `custom` is a deprecated qualifier; emit only under opt-in (--openusd-compat).
    if (opts.emit_custom && slot.is_custom()) os << "custom ";
    if (slot.is_uniform()) os << "uniform ";
    os << type_name << " " << name;
  };

  const Value* value = spec.property_value(slot.name_id);
  const bool has_value = value && !value->is_empty();
  // Connection targets live in the prim's connection map, NOT in the property
  // value (a connection-only attr has no authored default). A property may also
  // carry BOTH a value and a connection -> emit them as separate statements.
  const std::vector<Path>* conns = spec.connection(name);
  const bool has_conn = conns && !conns->empty();

  // Value statement (authored default).
  if (has_value && value->is_block()) {
    // Authored value block (`= None`): a typed opinion with no data that blocks
    // weaker values. Emit it verbatim (the declared type carries the type name).
    emit_decl();
    os << " = None";
    WritePropMeta(os, spec, slot.name_id, depth, opts);
    os << "\n";
  } else if (has_value) {
    emit_decl();
    PrintOptions print_opts;
    print_opts.float_precision = opts.float_precision;
    print_opts.double_precision = opts.double_precision;
    print_opts.max_array_elements = opts.max_elements_per_line;
    print_opts.compact = opts.compact;
    os << " = ";
    // Parallel build: offload a non-trivial array value to the worker pool, which
    // emits its complete `[ ... ]` (byte-identical to the inline PrintValue). Tiny
    // arrays and non-array values are formatted inline.
    if (segsink && value->is_array() &&
        ArrayElementCount(*value) >= kOffloadMinElems) {
      segsink->offload_array(value);
    } else {
      PrintValue(os, *value, print_opts);
    }
    WritePropMeta(os, spec, slot.name_id, depth, opts);
    os << "\n";
  }

  // Connection statement: `<type> <name>.connect = </path>` (or `[...]`).
  if (has_conn) {
    emit_decl();
    os << ".connect = ";
    if (conns->size() == 1) {
      os << "<" << (*conns)[0].str() << ">";
    } else {
      os << "[";
      for (size_t i = 0; i < conns->size(); ++i) {
        if (i) os << ", ";
        os << "<" << (*conns)[i].str() << ">";
      }
      os << "]";
    }
    // Property metadata attaches once; emit on the value line if present, else
    // here on the connection line.
    if (!has_value) WritePropMeta(os, spec, slot.name_id, depth, opts);
    os << "\n";
  }

  // Declared-only attribute (no value, no connection): emit the bare
  // declaration so typed-only / namespace-declared props round-trip.
  if (!has_value && !has_conn) {
    emit_decl();
    WritePropMeta(os, spec, slot.name_id, depth, opts);
    os << "\n";
  }
}

// Write relationship
void WriteRelationship(StreamWriter& os, const std::string& name,
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
void WritePrimSpec(StreamWriter& os, const PrimSpec& spec, const Layer& layer,
                   int depth, const USDAWriteOptions& opts,
                   SegmentSink* segsink = nullptr);

void WritePrimSpec(StreamWriter& os, const PrimSpec& spec, const Layer& layer,
                   int depth, const USDAWriteOptions& opts,
                   SegmentSink* segsink) {
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
                  !meta.variantSelection.empty() ||
                  !meta.variantSets().empty();

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
          // `a` is a composed arc string: "@asset@</prim>" (external),
          // "</prim>" (internal reference, no asset), or a bare asset path.
          // Emit external/internal forms as-is; only a bare asset path is
          // wrapped in @...@.
          if (!a.empty() && (a[0] == '@' || a[0] == '<'))
            os << a << ",\n";
          else
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
    } else if (!meta.variantSets().empty()) {
      // No explicit selections list, but per-set `selected` fields may be
      // authored: emit them so selections round-trip.
      std::vector<std::pair<std::string, std::string>> sels;
      for (const auto& vs : meta.variantSets()) {
        if (!vs.selected.empty()) sels.emplace_back(vs.name, vs.selected);
      }
      if (!sels.empty()) write_variants(sels);
    }

    // Variant set declaration: `prepend variantSets = ["lod", ...]`.
    if (!meta.variantSets().empty()) {
      WriteIndent(os, md, opts.indent);
      os << "prepend variantSets = [";
      for (size_t i = 0; i < meta.variantSets().size(); ++i) {
        if (i) os << ", ";
        os << EscapeString(meta.variantSets()[i].name);
      }
      os << "]\n";
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
    WriteProperty(os, *slot, spec, content_depth, opts, segsink);
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

  // Write variant set bodies:
  //   variantSet "lod" = { "high" { ...props... } "low" { } }
  // Covers option names, per-option properties and relationships (the shapes
  // the authoring API produces). Option-level composition arcs / nested sets /
  // content subtrees are not emitted here.
  for (const VariantSetData& vs : spec.meta().variantSets()) {
    os << "\n";
    WriteIndent(os, content_depth, opts.indent);
    os << "variantSet " << EscapeString(vs.name) << " = {\n";
    for (const VariantData& var : vs.variants) {
      WriteIndent(os, content_depth + 1, opts.indent);
      os << EscapeString(var.name) << " {\n";
      PrintOptions vpopts;
      vpopts.float_precision = opts.float_precision;
      vpopts.double_precision = opts.double_precision;
      for (const VariantProperty& vp : var.properties) {
        WriteIndent(os, content_depth + 2, opts.indent);
        if (vp.flags & PropSlot::kFlagUniform) os << "uniform ";
        const char* tn = GetTypeName(vp.value.type_id());
        os << (tn ? tn : "token");
        if (vp.value.is_array()) os << "[]";
        os << " " << vp.name << " = ";
        PrintValue(os, vp.value, vpopts);
        os << "\n";
      }
      for (const auto& rel : var.relationships) {
        WriteIndent(os, content_depth + 2, opts.indent);
        os << "rel " << rel.first;
        if (rel.second.size() == 1) {
          os << " = <" << rel.second[0].str() << ">";
        } else if (!rel.second.empty()) {
          os << " = [";
          for (size_t t = 0; t < rel.second.size(); ++t) {
            if (t) os << ", ";
            os << "<" << rel.second[t].str() << ">";
          }
          os << "]";
        }
        os << "\n";
      }
      // Variant CHILD prims. USDA-parsed variants keep them in the content
      // sub-layer (root "__self__"); crate-loaded variants keep them under a
      // bracketed holder prim in THIS layer. Emit whichever exists.
      if (var.content) {
        if (const PrimSpec* self = var.content->prim_at_path("/__self__")) {
          for (uint32_t ci : self->child_indices()) {
            const PrimSpec* child = var.content->prim(ci);
            if (!child) continue;
            os << "\n";
            WritePrimSpec(os, *child, *var.content, content_depth + 2, opts,
                          segsink);
          }
        }
      } else if (var.properties.empty()) {
        const std::string holder_path =
            spec.path().str() + "/{" + vs.name + "=" + var.name + "}";
        if (const PrimSpec* holder = layer.prim_at_path(holder_path)) {
          // Inline the holder's own opinions into the variant body.
          for (const PropSlot& hslot : holder->properties().slots()) {
            if (hslot.is_relationship()) continue;
            WriteProperty(os, hslot, *holder, content_depth + 2, opts,
                          segsink);
          }
          PropNameTable& htable = GetPropNameTable();
          for (const std::string& rel_name : holder->relationship_names()) {
            const std::vector<Path>* targets = holder->relationship(rel_name);
            if (!targets) continue;
            WriteRelationship(os, rel_name, *targets, *holder,
                              htable.find(rel_name), content_depth + 2, opts);
          }
          for (uint32_t ci : holder->child_indices()) {
            const PrimSpec* child = layer.prim(ci);
            if (!child) continue;
            os << "\n";
            WritePrimSpec(os, *child, layer, content_depth + 2, opts,
                          segsink);
          }
        }
      }
      WriteIndent(os, content_depth + 1, opts.indent);
      os << "}\n";
    }
    WriteIndent(os, content_depth, opts.indent);
    os << "}\n";
  }

  // Write children. When `segsink` is active (the parallel build walk), the same
  // sink propagates so each child's large array values are offloaded too.
  // Bracketed variant HOLDER prims ("{set=var}", crate representation) are not
  // real children: their names/selections are emitted via the variantSets
  // metadata + bodies above, and printing them as defs would be invalid USDA.
  for (uint32_t child_idx : spec.child_indices()) {
    const PrimSpec* child = layer.prim(child_idx);
    if (child) {
      const std::string& cn = child->name();
      if (cn.size() >= 2 && cn.front() == '{' && cn.back() == '}') continue;
      os << "\n";
      WritePrimSpec(os, *child, layer, content_depth, opts, segsink);
    }
  }

  WriteIndent(os, depth, opts.indent);
  os << "}\n";
}

// Serialize the layer-stage header metadata + all root prims serially (the
// classic streaming path). Shared by the serial entry and as a fallback.
void WriteStageBodySerial(StreamWriter& os, const Layer& layer,
                          const LayerMeta& meta, const USDAWriteOptions& opts) {
  WriteLayerMeta(os, meta, opts);
  for (uint32_t root_idx : layer.root_indices()) {
    const PrimSpec* root = layer.prim(root_idx);
    if (root) {
      WritePrimSpec(os, *root, layer, 0, opts);
      os << "\n";
    }
  }
}

#if defined(TINYUSDZ_ENABLE_THREAD)

// Resolve the effective worker count from the option (1 = serial; <=0 = auto;
// >1 = exactly that many). The parallel writer formats balanced segments (one
// giant array no longer pins a single worker), so throughput scales with cores;
// the auto default is capped at kAutoCap to stay reasonable on shared machines
// while still feeding many cores. An explicit request is honored as-is so callers
// can override on unusual hardware.
int ResolveWriteThreads(int requested) {
  if (requested == 1) return 1;
  if (requested > 1) return requested;
  constexpr int kAutoCap = 16;
  int hw = static_cast<int>(std::thread::hardware_concurrency());
  if (hw < 1) hw = 1;
  return std::min(hw, kAutoCap);
}

// Array-affecting subset of the write options (the value printer only consults
// the precision/limit/compact fields). Mirrors the inline construction in
// WriteProperty so frontier analysis sees the same chunkability decision.
PrintOptions MakeArrayPrintOpts(const USDAWriteOptions& opts) {
  PrintOptions p;
  p.float_precision = opts.float_precision;
  p.double_precision = opts.double_precision;
  p.max_array_elements = opts.max_elements_per_line;
  p.compact = opts.compact;
  return p;
}

// Parallel stage body. A single serial pre-order walk builds the document-ordered
// task list: cheap structural bytes accrue inline as Text, while every array value
// worth offloading (>= kOffloadMinElems elements) becomes WholeValue/Chunk task(s)
// referencing the borrowed array. A worker pool then formats the offloaded arrays
// concurrently -- this is where nearly all of a geometry-heavy stage's write time
// goes -- while the main thread writes the task results strictly in order. A
// bounded look-ahead window keeps in-flight result buffers (hence peak memory)
// small. Output is byte-identical to the serial writer regardless of thread count.
void WriteStageBodyParallel(StreamWriter& os, const Layer& layer,
                            const LayerMeta& meta, const USDAWriteOptions& opts,
                            int nthreads) {
  const PrintOptions po = MakeArrayPrintOpts(opts);

  // Pre-warm the global interning table on the main thread (its lazy init is the
  // only non-reentrant spot on the write path; reads are thread-safe after).
  (void)GetPropNameTable();

  // Phase 1 (serial, cheap): walk the whole stage, emitting structure as Text and
  // offloading array values. No giant array is formatted here, so this is fast.
  std::vector<WriteTask> tasks;
  std::deque<Value> holder;  // decoded buffers for materialized giant arrays
  {
    std::string cur;
    StreamWriter sw(&cur);
    SegmentSink sink;
    sink.tasks = &tasks;
    sink.cur = &cur;
    sink.po = &po;
    sink.holder = &holder;
    WriteLayerMeta(sw, meta, opts);
    for (uint32_t root_idx : layer.root_indices()) {
      const PrimSpec* root = layer.prim(root_idx);
      if (!root) continue;
      WritePrimSpec(sw, *root, layer, 0, opts, &sink);
      sw << "\n";
    }
    sink.flush_text();
  }

  const size_t m = tasks.size();
  if (m == 0) return;

  // Group the document-ordered tasks into ~K byte-balanced segments, each a
  // contiguous task run. A worker formats an entire segment (its structure +
  // arrays) into one buffer, so writing is just K large in-order copies -- cheap
  // synchronization (K is small) with balanced formatting (segments are equal
  // estimated bytes, so no worker gets stuck on one giant array's neighbourhood).
  auto scalar_bytes = [](const Value& v, uint64_t elems) -> uint64_t {
    // ~8 output chars per scalar component (digits + ", "); scale by components.
    size_t comps = GetComponentCount(v.type_id());
    if (comps < 1) comps = 1;
    return elems * uint64_t(comps) * 8u + 2u;
  };
  auto task_bytes = [&](const WriteTask& t) -> uint64_t {
    if (t.kind == WriteTask::Kind::Text) return t.text.size();
    if (t.kind == WriteTask::Kind::Chunk) return scalar_bytes(*t.value, t.hi - t.lo);
    return scalar_bytes(*t.value, ArrayElementCount(*t.value));  // WholeValue
  };
  uint64_t total_bytes = 0;
  for (const auto& t : tasks) total_bytes += task_bytes(t);
  const size_t k_target = std::max<size_t>(1, static_cast<size_t>(nthreads) * 64);
  const uint64_t seg_target = std::max<uint64_t>(1, total_bytes / k_target);
  std::vector<size_t> seg_begin;  // task index where each segment starts; +[m]
  seg_begin.push_back(0);
  uint64_t acc = 0;
  for (size_t i = 0; i < m; ++i) {
    acc += task_bytes(tasks[i]);
    if (acc >= seg_target && seg_begin.back() != i + 1) {
      seg_begin.push_back(i + 1);
      acc = 0;
    }
  }
  if (seg_begin.back() != m) seg_begin.push_back(m);
  const size_t K = seg_begin.size() - 1;

  // Per-segment list of decoded-buffer holder indices to free once that segment is
  // written. A buffer's freeing flag sits on its last chunk; by the time that
  // segment is consumed, all of the buffer's chunks (here and in earlier segments)
  // have been formatted, so the decode can be released -- bounding peak memory.
  std::vector<std::vector<int>> seg_free(K);
  {
    size_t seg = 0;
    for (size_t i = 0; i < m; ++i) {
      while (seg + 1 < K && i >= seg_begin[seg + 1]) ++seg;
      if (tasks[i].free_idx >= 0) seg_free[seg].push_back(tasks[i].free_idx);
    }
  }

  // Phase 2: workers format whole segments; the main thread writes segment buffers
  // in order. Lock-free: each segment is claimed once via `next`, produced into
  // results[k], published via ready[k]; the consumer spins (yielding) on ready[s]
  // and advances `consumed`, back-pressuring workers past W segments ahead.
  std::vector<std::string> results(K);
  std::vector<std::atomic<uint8_t>> ready(K);
  for (size_t i = 0; i < K; ++i) ready[i].store(0, std::memory_order_relaxed);
  std::atomic<size_t> next{0};
  std::atomic<size_t> consumed{0};
  const size_t W = static_cast<size_t>(nthreads) * 2 + 2;

  auto format_segment = [&](size_t k, std::string& buf) {
    buf.clear();
    StreamWriter sw(&buf);
    for (size_t i = seg_begin[k]; i < seg_begin[k + 1]; ++i) {
      WriteTask& t = tasks[i];
      switch (t.kind) {
        case WriteTask::Kind::Text:
          sw.write(t.text.data(), t.text.size());
          std::string().swap(t.text);  // free structure bytes once copied
          break;
        case WriteTask::Kind::Chunk:
          PrintArrayRangeToStream(sw, *t.value, po, t.lo, t.hi, t.open, t.close);
          break;
        case WriteTask::Kind::WholeValue:
          PrintValue(sw, *t.value, po);  // worker emits the full `[ ... ]`
          break;
      }
    }
  };

  auto worker = [&]() {
    std::string buf;
    for (;;) {
      size_t k = next.fetch_add(1, std::memory_order_relaxed);
      if (k >= K) break;
      while (k >= consumed.load(std::memory_order_acquire) + W) {
        std::this_thread::yield();
      }
      format_segment(k, buf);
      results[k] = std::move(buf);
      ready[k].store(1, std::memory_order_release);
    }
  };

  const int nw = std::min<int>(nthreads, static_cast<int>(K));
  std::vector<std::thread> pool;
  pool.reserve(nw);
  for (int t = 0; t < nw; ++t) pool.emplace_back(worker);

  for (size_t s = 0; s < K; ++s) {
    while (ready[s].load(std::memory_order_acquire) == 0) std::this_thread::yield();
    os.write(results[s].data(), results[s].size());
    std::string().swap(results[s]);  // free as we go
    for (int idx : seg_free[s]) holder[idx] = Value{};  // release decoded buffers
    consumed.store(s + 1, std::memory_order_release);
  }

  for (auto& th : pool) th.join();
}

#endif  // TINYUSDZ_ENABLE_THREAD

}  // anonymous namespace

// ============================================================
// Public API implementations
// ============================================================

// Core writer: targets a StreamWriter (any backend — stdio, file, string, or a
// host-supplied WASM/WASI sink). The std::ostream / file / string entry points
// below all wrap this.
USDAWriteResult WriteUSDA(StreamWriter& os, const Stage& stage,
                          const USDAWriteOptions& options) {
  USDAWriteResult result;

  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) {
    result.error = "Stage has no root layer";
    return result;
  }

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

#if defined(TINYUSDZ_ENABLE_THREAD)
  const int nthreads = ResolveWriteThreads(options.num_threads);
  if (nthreads > 1) {
    WriteStageBodyParallel(os, *root_layer, meta, options, nthreads);
  } else {
    WriteStageBodySerial(os, *root_layer, meta, options);
  }
#else
  WriteStageBodySerial(os, *root_layer, meta, options);
#endif

  os.flush();
  if (os.good()) {
    result.success = true;
    result.bytes_written = static_cast<size_t>(os.bytes_written());
  } else {
    result.error = "Stream write error";
  }
  return result;
}

USDAWriteResult WriteUSDA(std::ostream& os, const Stage& stage,
                           const USDAWriteOptions& options) {
  StreamWriter w(OstreamSink(os));
  return WriteUSDA(w, stage, options);
}

std::string WriteUSDAToString(const Stage& stage, const USDAWriteOptions& options) {
  std::string out;
  StreamWriter w(&out);
  WriteUSDA(w, stage, options);
  return out;
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

  // Native default backend: C stdio with a large (StreamWriter) buffer + blocked
  // writes, so library file output reaches disk at device speed.
  std::FILE* fp = std::fopen(filename, "wb");
  if (!fp) {
    result.error = "Failed to open file for writing: ";
    result.error += filename;
    return result;
  }
  {
    StreamWriter w(StdioSink(fp));
    result = WriteUSDA(w, stage, options);  // flushes the buffer before returning
  }
  if (std::fclose(fp) != 0 && result.success) {
    result.success = false;
    result.error = "Error writing to file";
  }
  return result;
}

std::string WriteLayerToString(const Layer& layer, const USDAWriteOptions& options) {
  std::string out;
  StreamWriter w(&out);
  WriteLayer(w, layer, options);
  return out;
}

USDAWriteResult WriteLayer(StreamWriter& os, const Layer& layer,
                            const USDAWriteOptions& options) {
  USDAWriteResult result;

  // Serialize the layer (header meta + root prims) using the same dispatch as
  // WriteUSDA(Stage): parallel subtree serialization when threads are enabled
  // and >1 worker is requested, else the serial streaming path. The Layer's own
  // metadata is used directly (no Stage->LayerMeta synthesis). Output is
  // byte-identical regardless of thread count.
#if defined(TINYUSDZ_ENABLE_THREAD)
  const int nthreads = ResolveWriteThreads(options.num_threads);
  if (nthreads > 1) {
    WriteStageBodyParallel(os, layer, layer.meta(), options, nthreads);
  } else {
    WriteStageBodySerial(os, layer, layer.meta(), options);
  }
#else
  WriteStageBodySerial(os, layer, layer.meta(), options);
#endif

  os.flush();
  if (os.good()) {
    result.success = true;
    result.bytes_written = static_cast<size_t>(os.bytes_written());
  } else {
    result.error = "Stream write error";
  }
  return result;
}

USDAWriteResult WriteLayer(std::ostream& os, const Layer& layer,
                           const USDAWriteOptions& options) {
  StreamWriter w(OstreamSink(os));
  return WriteLayer(w, layer, options);
}

USDAWriteResult WriteLayerToFile(const std::string& filename, const Layer& layer,
                                  const USDAWriteOptions& options) {
  USDAWriteResult result;

  std::FILE* fp = std::fopen(filename.c_str(), "wb");
  if (!fp) {
    result.error = "Failed to open file for writing: " + filename;
    return result;
  }
  {
    StreamWriter w(StdioSink(fp));
    result = WriteLayer(w, layer, options);
  }
  if (std::fclose(fp) != 0 && result.success) {
    result.success = false;
    result.error = "Error writing to file";
  }
  return result;
}

}  // namespace next
}  // namespace tinyusdz
