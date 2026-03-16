// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2022, Syoyo Fujita.
// Copyright 2023, Light Transport Entertainment Inc.
//
// USD ASCII pretty printer.
//
//
#include "pprinter.hh"

#include "prim-pprint.hh"
#include "prim-pprint-parallel.hh"
#include "prim-types.hh"
#include "layer.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdMtlx.hh"
#include "value-pprint.hh"
#include "timesamples-pprint.hh"
#include "stream-writer.hh"
//
#include "common-macros.inc"

// For fast int/float to ascii
// Default disabled.
// #define TINYUSDZ_LOCAL_USE_JEAIII_ITOA

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
#include "external/jeaiii_to_text.h"
#endif

// NOTE: Using dragonbox-based dtos() from str-util.hh for float-to-string
// conversion - it produces shortest representation for 100% of values


// TODO:
// - [ ] Print properties based on lexcographically(USDA)
// - [ ] Refactor variantSet stmt print.

namespace tinyusdz {

namespace {

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
void itoa(uint32_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int32_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(uint64_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int64_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
#endif

// Path quote
std::string pquote(const Path &p) {
  return wquote(p.full_path_name(), "<", ">");
}

}  // namespace
}  // namespace tinyusdz

namespace std {

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Visibility v) {
  ofs << to_string(v);
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Extent v) {
  ofs << to_string(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Interpolation v) {
  ofs << to_string(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Path &v) {
  ofs << tinyusdz::pquote(v);

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::LayerOffset &v) {
  bool print_offset{true};
  bool print_scale{true};

  if (std::fabs(v._offset) < std::numeric_limits<double>::epsilon()) {
    print_offset = false;
  }

  if (std::fabs(v._scale - 1.0) < std::numeric_limits<double>::epsilon()) {
    print_scale = false;
  }

  if (!print_offset && !print_scale) {
    // No need to print LayerOffset.
    return ofs;
  }

  // TODO: Do not print scale when it is 1.0
  ofs << "(";
  if (print_offset && print_scale) {
    ofs << "offset = " << tinyusdz::dtos(v._offset)
        << ", scale = " << tinyusdz::dtos(v._scale);
  } else if (print_offset) {
    ofs << "offset = " << tinyusdz::dtos(v._offset);
  } else {  // print_scale
    ofs << "scale = " << tinyusdz::dtos(v._scale);
  }
  ofs << ")";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Reference &v) {
  // For internal references (no asset path, just prim path), don't output "@@"
  if (!v.asset_path.GetAssetPath().empty()) {
    ofs << v.asset_path;
  }
  if (v.prim_path.is_valid()) {
    ofs << v.prim_path;
  }
  ofs << v.layerOffset;
  if (!v.customData.empty()) {
    ofs << tinyusdz::print_customData(v.customData, "customData",
                                      /* indent */ 0);
  }

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Payload &v) {
  if (v.is_none()) {
    ofs << "None";
  } else {
    // For internal payloads (no asset path, just prim path), don't output "@@"
    if (!v.asset_path.GetAssetPath().empty()) {
      ofs << v.asset_path;
    }
    if (v.prim_path.is_valid()) {
      ofs << v.prim_path;
    }
    ofs << v.layerOffset;
  }

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::SubLayer &v) {
  ofs << v.assetPath << v.layerOffset;

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::StringData &v) {
   ofs << tinyusdz::buildEscapedAndQuotedStringForUSDA(v.value);
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Layer &layer) {
  ofs << to_string(layer);
  return ofs;
}

}  // namespace std

namespace tinyusdz {

namespace pprint {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif

static std::string sIndentString = "    ";

#ifdef __clang__
#pragma clang diagnostic pop
#endif

std::string Indent(uint32_t n) {
  std::stringstream ss;

  for (uint32_t i = 0; i < n; i++) {
    ss << sIndentString;
  }

  return ss.str();
}

void SetIndentString(const std::string &s) { sIndentString = s; }

}  // namespace pprint

// Helper: print ".connect = <path>" for any attribute with connections
static std::string fmt_connections(const std::vector<Path> &paths) {
  std::stringstream s;
  if (paths.size() == 1) {
    s << paths[0];
  } else if (paths.empty()) {
    s << "[InternalError]";
  } else {
    s << paths;
  }
  return s.str();
}

// Helper: print attribute metadata block
static void print_attr_metas_block(std::stringstream &ss,
                                    const AttrMeta &metas,
                                    const uint32_t indent) {
  if (metas.authored()) {
    ss << " (\n"
       << print_attr_metas(metas, indent + 1) << pprint::Indent(indent)
       << ")";
  }
}

template <typename T>
std::string print_typed_timesamples(const TypedTimeSamples<T> &v,
                                    const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  const auto &times = v.get_times();
  const auto &values = v.get_values();
  const auto &blocked = v.get_blocked();

  for (size_t i = 0; i < times.size(); i++) {
    ss << pprint::Indent(indent + 1) << times[i] << ": ";
    if (blocked[i]) {
      ss << "None";
    } else {
      ss << values[i];
    }
    ss << ",\n";
  }
#else
  const auto &samples = v.get_samples();

  for (size_t i = 0; i < samples.size(); i++) {
    ss << pprint::Indent(indent + 1) << samples[i].t << ": ";
    if (samples[i].blocked) {
      ss << "None";
    } else {
      ss << samples[i].value;
    }
    ss << ",\n";
  }
#endif

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

template <typename T>
std::string print_typed_token_timesamples(const TypedTimeSamples<T> &v,
                                          const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  const auto &times = v.get_times();
  const auto &values = v.get_values();
  const auto &blocked = v.get_blocked();

  for (size_t i = 0; i < times.size(); i++) {
    ss << pprint::Indent(indent + 1) << times[i] << ": ";
    if (blocked[i]) {
      ss << "None";
    } else {
      ss << quote(to_string(values[i]));
    }
    ss << ",\n";
  }
#else
  const auto &samples = v.get_samples();

  for (size_t i = 0; i < samples.size(); i++) {
    ss << pprint::Indent(indent + 1) << samples[i].t << ": ";
    if (samples[i].blocked) {
      ss << "None";
    } else {
      ss << quote(to_string(samples[i].value));
    }
    ss << ",\n";
  }
#endif

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

static std::string print_str_timesamples(const TypedTimeSamples<std::string> &v,
                                         const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  const auto &times = v.get_times();
  const auto &values = v.get_values();
  const auto &blocked = v.get_blocked();

  for (size_t i = 0; i < times.size(); i++) {
    ss << pprint::Indent(indent + 1) << times[i] << ": ";
    if (blocked[i]) {
      ss << "None";
    } else {
      ss << buildEscapedAndQuotedStringForUSDA(values[i]);
    }
    ss << ",\n";
  }
#else
  const auto &samples = v.get_samples();

  for (size_t i = 0; i < samples.size(); i++) {
    ss << pprint::Indent(indent + 1) << samples[i].t << ": ";
    if (samples[i].blocked) {
      ss << "None";
    } else {
      ss << buildEscapedAndQuotedStringForUSDA(samples[i].value);
    }
    ss << ",\n";
  }
#endif

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

template <typename T>
std::string print_animatable_default(const Animatable<T> &v,
                             const uint32_t indent = 0) {
  (void)indent;

  std::stringstream ss;

  if (v.is_blocked()) {
    ss << "None";
  }

  if (v.has_value()) {
    T a;
    if (!v.get_scalar(&a)) {
      return "[Animatable: InternalError]";
    }
    ss << a;
  }

  return ss.str();
}

template <typename T>
std::string print_animatable_timesamples(const Animatable<T> &v,
                             const uint32_t indent = 0) {
  std::stringstream ss;

  if (v.has_timesamples()) {
    ss << print_typed_timesamples(v.get_timesamples(), indent);
  }

  return ss.str();
}


namespace {

// Print a single reference listop (e.g., "prepend references = ...")
std::string print_reference_listop(const prim::ReferenceListOp &ref_op,
                                   const uint32_t indent) {
  std::stringstream ss;

  auto listEditQual = std::get<0>(ref_op);
  auto vars = std::get<1>(ref_op);

  ss << pprint::Indent(indent);

  if (listEditQual != ListEditQual::ResetToExplicit) {
    ss << to_string(listEditQual) << " ";
  }

  ss << "references = ";

  if (vars.empty()) {
    ss << "None";
  } else {
    if (vars.size() == 1) {
      ss << vars[0];
    } else {
      ss << vars;
    }
  }
  ss << "\n";

  return ss.str();
}

// Print all reference listops (supports multiple listops per prim)
std::string print_references(const prim::ReferenceList &references,
                             const uint32_t indent) {
  std::stringstream ss;
  for (const auto &ref_op : references) {
    ss << print_reference_listop(ref_op, indent);
  }
  return ss.str();
}

std::string print_rel_only(const Relationship &rel, const std::string &name,
                           uint32_t indent) {
  std::stringstream ss;

  ss << "rel " << name;

  if (!rel.has_value()) {
    // nothing todo
  } else if (rel.is_path()) {
    ss << " = " << rel.targetPath;
  } else if (rel.is_pathvector()) {
    // Single-element path vector: print as scalar path without brackets
    if (rel.targetPathVector.size() == 1) {
      ss << " = " << rel.targetPathVector[0];
    } else {
      ss << " = " << rel.targetPathVector;
    }
  } else if (rel.is_blocked()) {
    ss << " = None";
  } else {
    ss << "[InternalErrror]";
  }

  if (rel.metas().authored()) {
    ss << " (\n"
       << print_attr_metas(rel.metas(), indent + 1) << pprint::Indent(indent)
       << ")";
  }

  ss << "\n";

  return ss.str();
}

std::string print_relationship(const Relationship &rel,
                               const ListEditQual &qual, const bool custom,
                               const std::string &name, uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent);

  // USD spec order: [listop] [custom] [variability] rel name
  // List editing qualifier comes first
  if (qual != ListEditQual::ResetToExplicit) {
    ss << to_string(qual) << " ";
  }

  if (custom) {
    ss << "custom ";
  }

  if (rel.is_varying_authored()) {
    ss << "varying ";
  }

  ss << print_rel_only(rel, name, indent);

  return ss.str();
}

}  // namespace

// Print a single payload listop (e.g., "prepend payload = ...")
static std::string print_payload_listop(const prim::PayloadListOp &payload_op,
                                        const uint32_t indent) {
  std::stringstream ss;

  auto listEditQual = std::get<0>(payload_op);
  auto vars = std::get<1>(payload_op);

  ss << pprint::Indent(indent);

  if (listEditQual != ListEditQual::ResetToExplicit) {
    ss << to_string(listEditQual) << " ";
  }

  ss << "payload = ";
  if (vars.empty()) {
    ss << "None";
  } else {
    if (vars.size() == 1) {
      ss << vars[0];
    } else {
      ss << vars;
    }
  }
  ss << "\n";

  return ss.str();
}

// Print all payload listops (supports multiple listops per prim)
std::string print_payload(const prim::PayloadList &payload,
                          const uint32_t indent) {
  std::stringstream ss;
  for (const auto &payload_op : payload) {
    ss << print_payload_listop(payload_op, indent);
  }
  return ss.str();
}

std::string print_prim_metas(const PrimMeta &meta, const uint32_t indent) {
  std::stringstream ss;

  if (meta.has_active()) {
    ss << pprint::Indent(indent)
       << "active = " << to_string(meta.get_active()) << "\n";
  }

  if (meta.has_clips()) {
    ss << print_customData(meta.get_clips(), "clips", indent);
  }

  if (meta.has_instanceable()) {
    ss << pprint::Indent(indent)
       << "instanceable = " << to_string(meta.get_instanceable()) << "\n";
  }

  if (meta.has_hidden()) {
    ss << pprint::Indent(indent)
       << "hidden = " << to_string(meta.get_hidden()) << "\n";
  }

  if (meta.has_kind()) {
    ss << pprint::Indent(indent) << "kind = " << quote(meta.get_kind()) << "\n";
  }

  if (meta.has_sceneName()) {
    ss << pprint::Indent(indent)
       << "sceneName = " << quote(meta.get_sceneName()) << "\n";
  }

  if (meta.has_displayName()) {
    ss << pprint::Indent(indent)
       << "displayName = " << quote(meta.get_displayName()) << "\n";
  }

  if (meta.has_assetInfo()) {
    ss << print_customData(meta.get_assetInfo(), "assetInfo", indent);
  }

  if (meta.inherits) {
    // Print all inherits listops
    for (const auto &inherit_op : meta.inherits.value()) {
      ss << pprint::Indent(indent);
      auto listEditQual = std::get<0>(inherit_op);
      auto var = std::get<1>(inherit_op);

      if (listEditQual != ListEditQual::ResetToExplicit) {
        ss << to_string(listEditQual) << " ";
      }

      if (var.size() == 1) {
        // print as scalar
        ss << "inherits = " << var[0];
      } else {
        ss << "inherits = " << var;
      }
      ss << "\n";
    }
  }

  if (meta.specializes) {
    // Print all specializes listops
    for (const auto &specialize_op : meta.specializes.value()) {
      ss << pprint::Indent(indent);
      auto listEditQual = std::get<0>(specialize_op);
      auto var = std::get<1>(specialize_op);

      if (listEditQual != ListEditQual::ResetToExplicit) {
        ss << to_string(listEditQual) << " ";
      }

      if (var.size() == 1) {
        // print as scalar
        ss << "specializes = " << var[0];
      } else {
        ss << "specializes = " << var;
      }
      ss << "\n";
    }
  }

  if (meta.references) {
    ss << print_references(meta.references.value(), indent);
  }

  if (meta.payload) {
    ss << print_payload(meta.payload.value(), indent);
  }

  // TODO: only print in usdShade Prims.
  if (meta.has_sdrMetadata()) {
    ss << print_customData(meta.get_sdrMetadata(), "sdrMetadata", indent);
  }

  if (meta.variants) {
    ss << print_variantSelectionMap(meta.variants.value(), indent);
  }

  if (meta.variantSets) {
    // Print all variantSets listops
    for (const auto &variantSets_op : meta.variantSets.value()) {
      ss << pprint::Indent(indent);
      auto listEditQual = std::get<0>(variantSets_op);
      const std::vector<std::string> &vs = std::get<1>(variantSets_op);  // string[]

      if (listEditQual != ListEditQual::ResetToExplicit) {
        ss << to_string(listEditQual) << " ";
      }

      ss << "variantSets = ";

      if (vs.empty()) {
        ss << "None";
      } else if (vs.size() == 1) {
        // Single element: print as scalar (without brackets)
        ss << quote(vs[0]);
      } else {
        ss << to_string(vs);
      }

      ss << "\n";
    }
  }

  if (meta.has_apiSchemas()) {
    auto schemas = meta.get_apiSchemas();

    // Check if there are any schemas (known or unknown) to print
    if (schemas.names.size() || schemas.unknownSchemas.size()) {
      ss << pprint::Indent(indent) << to_string(schemas.listOpQual)
         << " apiSchemas = [";

      bool first = true;

      // Print unknown schemas first (they typically appear first in USD files)
      for (size_t i = 0; i < schemas.unknownSchemas.size(); i++) {
        if (!first) {
          ss << ", ";
        }
        first = false;

        auto schemaName = std::get<0>(schemas.unknownSchemas[i]);
        ss << "\"" << schemaName;

        auto instanceName = std::get<1>(schemas.unknownSchemas[i]);
        if (!instanceName.empty()) {
          ss << ":" << instanceName;
        }

        ss << "\"";
      }

      // Print known schemas
      for (size_t i = 0; i < schemas.names.size(); i++) {
        if (!first) {
          ss << ", ";
        }
        first = false;

        auto name = std::get<0>(schemas.names[i]);
        ss << "\"" << to_string(name);

        auto instanceName = std::get<1>(schemas.names[i]);

        if (!instanceName.empty()) {
          ss << ":" << instanceName;
        }

        ss << "\"";
      }
      ss << "]\n";
    }
  }

  if (meta.has_doc()) {
    ss << pprint::Indent(indent) << "doc = "
       << buildEscapedAndQuotedStringForUSDA(meta.get_doc().value) << "\n";
  }

  if (meta.has_comment()) {
    // Output with or without "comment =" prefix based on how it was parsed
    ss << pprint::Indent(indent);
    if (meta.get_comment().has_comment_prefix) {
      ss << "comment = ";
    }
    ss << buildEscapedAndQuotedStringForUSDA(meta.get_comment().value) << "\n";
  }

  if (meta.has_customData()) {
    ss << print_customData(meta.get_customData(), "customData", indent);
  }

  for (const auto &item : meta.unregisteredMetas) {
    // Write verbatim — the stored string already contains quotes if the
    // original value was a quoted string.  Non-string values (numbers,
    // arrays, None, tuples, …) are stored without quotes and must be
    // written back without quotes.  This matches OpenUSD behaviour.
    ss << pprint::Indent(indent) << item.first << " = " << item.second << "\n";
  }

  // TODO: deprecate meta.meta and remove it.
  for (const auto &item : meta.meta) {
    ss << print_meta(item.second, indent + 1, true, item.first);
  }

  // for (const auto &item : meta.stringData) {
  //   ss << pprint::Indent(indent) << to_string(item) << "\n";
  // }

  return ss.str();
}

std::string print_attr_metas(const AttrMeta &meta, const uint32_t indent) {
  std::stringstream ss;

  if (meta.has_interpolation()) {
    ss << pprint::Indent(indent)
       << "interpolation = " << to_string(meta.get_interpolation())
       << "\n";
  }

  if (meta.has_elementSize()) {
    ss << pprint::Indent(indent)
       << "elementSize = " << to_string(meta.get_elementSize()) << "\n";
  }

  if (meta.has_bindMaterialAs()) {
    ss << pprint::Indent(indent)
       << "bindMaterialAs = " << to_string(meta.get_bindMaterialAs())
       << "\n";
  }

  if (meta.has_connectability()) {
    ss << pprint::Indent(indent)
       << "connectability = " << to_string(meta.get_connectability())
       << "\n";
  }

  if (meta.has_displayName()) {
    ss << pprint::Indent(indent)
       << "displayName = " << quote(meta.get_displayName()) << "\n";
  }

  if (meta.has_displayGroup()) {
    ss << pprint::Indent(indent)
       << "displayGroup = " << quote(meta.get_displayGroup()) << "\n";
  }

  if (meta.has_outputName()) {
    ss << pprint::Indent(indent)
       << "outputName = " << to_string(meta.get_outputName()) << "\n";
  }

  if (meta.has_renderType()) {
    ss << pprint::Indent(indent)
       << "renderType = " << to_string(meta.get_renderType()) << "\n";
  }

  if (meta.has_sdrMetadata()) {
    ss << pprint::Indent(indent)
       << print_customData(meta.get_sdrMetadata(), "sdrMetadata", indent);
  }

  if (meta.has_hidden()) {
    ss << pprint::Indent(indent)
       << "hidden = " << to_string(meta.get_hidden()) << "\n";
  }

  if (meta.has_comment()) {
    ss << pprint::Indent(indent)
       << "comment = " << to_string(meta.get_comment()) << "\n";
  }

  if (meta.has_weight()) {
    ss << pprint::Indent(indent) << "weight = " << dtos(meta.get_weight())
       << "\n";
  }

  if (meta.has_customData()) {
    ss << print_customData(meta.get_customData(), "customData", indent);
  }

  // other user defined metadataum.
  auto is_known_key = [](const std::string &key) {
    // Keys already printed above
    constexpr const char* known[] = {
      "interpolation", "elementSize", "bindMaterialAs", "connectability",
      "displayName", "displayGroup", "outputName", "renderType",
      "sdrMetadata", "hidden", "comment", "weight", "customData"
    };
    for (const char* k : known) {
      if (key == k) return true;
    }
    return false;
  };
  for (const auto &item : meta.data()) {
    if (is_known_key(item.first)) continue;
    // attribute meta does not emit type_name
    ss << print_meta(item.second, indent, /* emit_type_name */false, item.first);
  }

  for (const auto &item : meta.stringData) {
    ss << pprint::Indent(indent) << to_string(item) << "\n";
  }

  return ss.str();
}

template <typename T>
std::string print_typed_attr(const TypedAttribute<Animatable<T>> &attr,
                             const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    bool is_value_empty = attr.is_value_empty();
    bool is_connection = attr.is_connection();
    bool has_default{false};
    bool has_timesamples{false};
    bool is_timesamples{false};
    const auto &pv = attr.get_value();

    has_default = (pv && pv.value().has_default());
    has_timesamples = (pv && pv.value().has_timesamples());
    is_timesamples = (pv && pv.value().is_timesamples());
    (void)is_timesamples;  // Used only in debug logging

    DCOUT("name " << name);
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_timesamples " << has_timesamples);
    DCOUT("has_default " << has_default);

    //
    // Emit default value(includes ValueBlock and empty definition) and metadata
    //
    // float a METADATA
    // float a = None METADATA
    // float a = 1.5 METADATA
    //
    // Also emit this line if the attribute contains metadata
    // Do not emit when Attribute is connection only or timesamples only (no default).
    // USD allows both default value AND timeSamples - default is used when no sample exists.
    bool should_emit_declaration =
        attr.metas().authored() ||
        attr.is_blocked() ||
        has_default ||  // Emit default even if timeSamples exist (USD allows both)
        is_value_empty ||
        ((!is_connection) && (!has_timesamples));
    if (should_emit_declaration) {

      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (has_default) {
        T a;
        if (pv.value().get_scalar(&a)) {
          std::stringstream val_ss;
          val_ss << a;
          std::string val_str = val_ss.str();
          // Only print " = value" if value prints something
          if (!val_str.empty()) {
            ss << " = " << val_str;
          }
        }
        // Else: no value to print (skip " = [InternalError]" for graceful handling)
      } else { // is_value_empty
      }

      print_attr_metas_block(ss, attr.metas(), indent);
      ss << "\n";
    }

    // timesamples
    if (has_timesamples) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;
      ss << ".timeSamples = "
         << print_typed_timesamples(pv.value().get_timesamples(), indent);
      ss << "\n";
    }

    // connection
    if (attr.has_connections()) {

      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;

      ss << ".connect = " << fmt_connections(attr.get_connections()) << "\n";
    }

  }

  return ss.str();
}

static std::string print_str_attr(
    const TypedAttribute<Animatable<std::string>> &attr,
    const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    bool is_value_empty = attr.is_value_empty();
    bool is_connection = attr.is_connection();
    bool has_default{false};
    bool has_timesamples{false};
    bool is_timesamples{false};
    const auto &pv = attr.get_value();
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_default " << has_default);

    has_default = (pv && pv.value().has_default());
    has_timesamples = (pv && pv.value().has_timesamples());
    is_timesamples = (pv && pv.value().is_timesamples());

    if (attr.metas().authored() || attr.is_blocked() || has_default || is_value_empty || ((!is_connection) && (!is_timesamples))) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (pv.has_value()) {

        std::string a;
        if (pv.value().get_scalar(&a)) {
          // Do not use operator<<(std::string)
          ss << " = " << tinyusdz::buildEscapedAndQuotedStringForUSDA(a);
        } else {
          ss << " = [InternalError]";
        }
      }

      print_attr_metas_block(ss, attr.metas(), indent);
      ss << "\n";
    }

    if (has_timesamples) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      ss << ".timeSamples = "
               << print_str_timesamples(pv.value().get_timesamples(), indent);

      ss << "\n";
    }

    if (attr.has_connections()) {

      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      ss << ".connect = ";
      const std::vector<Path> &paths = attr.get_connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

      ss << "\n";
    }
  }

  return ss.str();
}


template <typename T>
std::string print_typed_attr(const TypedAttribute<T> &attr,
                             const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    if (attr.metas().authored() || attr.is_blocked() || attr.has_value() || attr.is_value_empty() || (!attr.is_connection())) {
      ss << pprint::Indent(indent);
      ss << "uniform ";
      ss << value::TypeTraits<T>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (attr.is_value_empty()) {
        // nothing to do
      } else {
        auto pv = attr.get_value();
        if (pv) {
          ss << " = " << pv.value();
        }
      }

      print_attr_metas_block(ss, attr.metas(), indent);
      ss << "\n";
    }

    if (attr.has_connections()) {
      ss << pprint::Indent(indent);
      ss << "uniform ";
      ss << value::TypeTraits<T>::type_name() << " " << name;
      ss << ".connect = ";

      const std::vector<Path> &paths = attr.get_connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

      ss << "\n";
    }
  }

  return ss.str();
}


template <typename T>
std::string print_typed_attr(
    const TypedAttributeWithFallback<Animatable<T>> &attr,
    const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    const auto &v = attr.get_value();

    bool is_connection = attr.is_connection();
    bool is_timesamples = v.is_timesamples();
    bool has_timesamples = v.has_timesamples();
    // Note: attr.has_value() always returns true for TypedAttributeWithFallback (due to fallback)
    // Use v.has_value() to check if Animatable has an explicit default value
    bool has_default = v.has_value();
    bool is_value_empty = attr.is_value_empty();
    (void)is_timesamples;  // Used only in debug logging

    DCOUT("name " << name);
    DCOUT("is_value_empty " << attr.is_value_empty());
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_timesamples " << has_timesamples);
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("has_default " << has_default);

    // Emit declaration line if:
    // - Has metadata to emit
    // - Has explicit default value (in the Animatable, not just fallback)
    // - Is empty declaration without connection (e.g., "float myval;")
    // - Not a connection-only and not timeSamples-only attribute
    // Note: Use has_timesamples instead of is_timesamples because is_timesamples
    // returns false when there's a default/fallback value (even if timeSamples exist)
    // Note: Check is_value_empty first, since get_value() returns the fallback for empty attrs,
    // and the fallback's has_value() may return true even though no value was authored
    // Note: For connection-only attributes (is_value_empty && has_connections), skip the declaration line
    bool has_connections = attr.has_connections();
    bool is_connection_only = is_value_empty && has_connections;
    bool is_pure_empty_decl = is_value_empty && !has_connections && !has_timesamples;

    if (attr.metas().authored() || (!is_value_empty && has_default) || is_pure_empty_decl || ((!is_connection) && (!has_timesamples) && !is_connection_only)) {
      if (is_value_empty) {
        // declare only - no value was authored (pure empty declaration)
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
      } else if (has_default) {
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
        ss << " = " << print_animatable_default(v, indent);
      } else {
        // declare only (for cases without connection or timesamples)
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
      }

      print_attr_metas_block(ss, attr.metas(), indent);
      ss << "\n";
    }

    if (v.has_timesamples()) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;
      ss << ".timeSamples = " << print_animatable_timesamples(v, indent);
      ss << "\n";
    }

    if (attr.has_connections()) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;
      ss << ".connect = ";

      const std::vector<Path> &paths = attr.get_connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

      ss << "\n";
    }

  }

  return ss.str();
}

template <typename T>
std::string print_typed_terminal_attr(const TypedTerminalAttribute<T> &attr,
                                      const std::string &name,
                                      const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {
    ss << pprint::Indent(indent);

    if (attr.has_actual_type()) {
      ss << attr.get_actual_type_name() << " " << name;
    } else {
      ss << value::TypeTraits<T>::type_name() << " " << name;
    }

    if (attr.metas().authored()) {
      ss << " (\n"
         << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
         << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

template <typename T>
std::string print_typed_attr(const TypedAttributeWithFallback<T> &attr,
                             const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    // default
    if (attr.metas().authored() || attr.is_blocked() || attr.is_value_empty() || (!attr.is_connection())) {
      ss << pprint::Indent(indent);
      ss << "uniform ";
      ss << value::TypeTraits<T>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (attr.is_value_empty()) {
        // Definition only - no value to output
      } else {
        ss << " = " << attr.get_value();
      }

      print_attr_metas_block(ss, attr.metas(), indent);
      ss << "\n";
    }

    if (attr.has_connections()) {
      ss << pprint::Indent(indent);
      ss << "uniform ";
      ss << value::TypeTraits<T>::type_name() << " " << name;
      ss << ".connect = ";

      const std::vector<Path> &paths = attr.get_connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }
    }
  }

  return ss.str();
}

template <typename T>
std::string print_typed_token_attr(
    const TypedAttributeWithFallback<Animatable<T>> &attr,
    const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    const auto &v = attr.get_value();

    if (attr.metas().authored() || v.has_value() || attr.is_value_empty()) {
      ss << pprint::Indent(indent);
      ss << "token " << name << " = ";
      if (v.is_blocked()) {
        ss << "None";
      } else {
        T a;
        if (v.get_scalar(&a)) {
          ss << quote(to_string(a));
        } else { 
          ss << "[Animatable: InternalError]";
        }
      }

      print_attr_metas_block(ss, attr.metas(), indent);
      ss << "\n";
    }

    if (v.has_timesamples()) {

      ss << pprint::Indent(indent);
      ss << "token " << name << ".timeSamples = ";

      ss << print_typed_token_timesamples(v.get_timesamples(), indent);
      ss << "\n";
    }


    if (attr.has_connections()) {
      ss << pprint::Indent(indent);

      ss << "token " << name;

      ss << ".connect = ";
      const std::vector<Path> &paths = attr.get_connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

      ss << "\n";
    }

  }


  return ss.str();
}

template <typename T>
std::string print_typed_token_attr(const TypedAttributeWithFallback<T> &attr,
                                   const std::string &name,
                                   const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    ss << pprint::Indent(indent);

    ss << "uniform token " << name;

    if (attr.is_blocked()) {
      ss << " = None";
    } else if (attr.is_value_empty()) {
      // Definition only - no value to output
    } else {
      ss << " = " << quote(to_string(attr.get_value()));
    }

    if (attr.metas().authored()) {
      ss << " (\n"
         << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
         << ")";
    }
    ss << "\n";

    if (attr.has_connections()) {
      ss << pprint::Indent(indent);

      ss << "token " << name;

      ss << ".connect = " << fmt_connections(attr.get_connections()) << "\n";
    }
  }

  return ss.str();
}

std::string print_timesamples(const value::TimeSamples &v,
                              const uint32_t indent) {
  // Use the new pprint_timesamples function from timesamples-pprint,
  // which handles both binary and generic value-backed cases efficiently.
  std::string result = pprint_timesamples(v, indent);

  // Add a trailing newline if not present (for consistency with other pprinter functions)
  if (!result.empty() && result.back() != '\n') {
    result += '\n';
  }

  return result;
}

std::string print_rel_prop(const Property &prop, const std::string &name,
                           uint32_t indent) {
  std::stringstream ss;

  if (!prop.is_relationship()) {
    return ss.str();
  }

  ss << pprint::Indent(indent);

  // USD spec order: [listop] [custom] [variability] rel name
  // List editing qualifier comes first
  if (prop.get_listedit_qual() != ListEditQual::ResetToExplicit) {
    ss << to_string(prop.get_listedit_qual()) << " ";
  }

  if (prop.has_custom()) {
    ss << "custom ";
  }

  const Relationship &rel = prop.get_relationship();
  if (rel.is_varying_authored()) {
    ss << "varying ";
  }

  ss << print_rel_only(rel, name, indent);

  return ss.str();
}

std::string print_prop(const Property &prop, const std::string &prop_name,
                       uint32_t indent) {
  std::stringstream ss;

  if (prop.is_relationship()) {
    ss << print_rel_prop(prop, prop_name, indent);

    // Attribute or AttributeConnection
  } else if (prop.is_attribute()) {
    const Attribute &attr = prop.get_attribute();

    //
    // May print multiple times.
    // e.g.
    // float var = 1.0
    // float var.timeSamples = ...
    // float var.connect = <...>
    //
    // timeSamples and connect cannot have attrMeta
    // 

    // Print attribute if it has metadata, has a value, OR is just typed (but not connection-only)
    // NOTE: Some attributes (like outputs:out) may be typed but not have a value
    // Skip printing declaration if this is a connection-only attribute (will be printed in the connection section below)
    bool is_connection_only = attr.has_connections() && !attr.has_value() && !attr.has_timesamples() && !attr.metas().authored();

    if ((attr.metas().authored() || attr.has_value() || !attr.type_name().empty()) && !is_connection_only) {

      ss << pprint::Indent(indent);

      if (prop.has_custom()) {
        ss << "custom ";
      }

      if (attr.variability() == Variability::Uniform) {
        ss << "uniform ";
      } else if (attr.is_varying_authored()) {
        // For Attribute, `varying` is the default variability and does not shown
        // in USDA do nothing
      }

      std::string ty;

      ty = attr.type_name();
      ss << ty << " " << prop_name;

      if (!attr.has_value()) {
        // Nothing to do
      } else {
        // has value content

        if (attr.is_blocked()) {
          ss << " = None";
        } else {
          // default value
          std::string value_str = value::pprint_value(attr.get_var().value_raw());
          // Only print " = value" if value_str is not empty
          // (value could be typed but empty when only timeSamples are set)
          if (!value_str.empty()) {
            ss << " = " << value_str;
          }
        }
      }

      if (prop.get_attribute().metas().authored()) {
        ss << " (\n"
           << print_attr_metas(prop.get_attribute().metas(), indent + 1)
           << pprint::Indent(indent) << ")";
      }

      ss << "\n";
    }

    // Check if timeSamples were authored (even if empty)
    // An authored but empty timeSamples will have a valid type_id but size=0
    bool has_timesamples_authored = (attr.has_timesamples() || attr.get_var().ts_raw().type_id() != 0);

    if (has_timesamples_authored && (attr.variability() != Variability::Uniform)) {

      ss << pprint::Indent(indent);

      if (prop.has_custom()) {
        ss << "custom ";
      }

      std::string ty;

      ty = attr.type_name();
      ss << ty << " " << prop_name;

      ss << ".timeSamples";

      ss << " = ";

      ss << print_timesamples(attr.get_var().ts_raw(), indent);

      ss << "\n";
    }

    if (attr.has_connections()) {

      ss << pprint::Indent(indent);

      if (prop.has_custom()) {
        ss << "custom ";
      }

      if (attr.variability() == Variability::Uniform) {
        ss << "uniform ";
      } else if (attr.is_varying_authored()) {
        // For Attribute, `varying` is the default variability and does not shown
        // in USDA do nothing
      }

      std::string ty;

      ty = attr.type_name();
      ss << ty << " " << prop_name;

      ss << ".connect = ";

      const std::vector<Path> &paths = attr.connections();
      if (paths.size() == 1) {
        ss << paths[0];
      } else if (paths.size() == 0) {
        ss << "[InternalError]";
      } else {
        ss << paths;
      }

      ss << "\n";
    }

  } else {
    ss << "[Invalid Property] " << prop_name << "\n";
  }

  return ss.str();
}

std::string print_props(const std::map<std::string, Property> &props,
                        uint32_t indent) {
  std::stringstream ss;

  for (const auto &item : props) {
    const Property &prop = item.second;

    ss << print_prop(prop, item.first, indent);
  }

  return ss.str();
}

// Print user-defined (custom) properties.
std::string print_props(const std::map<std::string, Property> &props,
                        std::set<std::string> &tok_table,
                        const std::vector<value::token> &propNames,
                        uint32_t indent) {
  std::stringstream ss;

  if (propNames.size()) {
    for (size_t i = 0; i < propNames.size(); i++) {
      if (tok_table.count(propNames[i].str())) {
        continue;
      }

      const auto it = props.find(propNames[i].str());
      if (it != props.end()) {
        ss << print_prop(it->second, it->first, indent);

        tok_table.insert(propNames[i].str());
      }
    }
  } else {
    ss << print_props(props, indent);
  }

  return ss.str();
}

std::string print_xformOpOrder(const std::vector<XformOp> &xformOps,
                               const uint32_t indent) {
  std::stringstream ss;

  if (xformOps.size()) {
    ss << pprint::Indent(indent) << "uniform token[] xformOpOrder = [";
    for (size_t i = 0; i < xformOps.size(); i++) {
      if (i > 0) {
        ss << ", ";
      }

      auto xformOp = xformOps[i];
      ss << "\"";
      if (xformOp.inverted) {
        ss << "!invert!";
      }
      ss << to_string(xformOp.op_type);
      if (!xformOp.suffix.empty()) {
        ss << ":" << xformOp.suffix;
      }
      ss << "\"";
    }
    ss << "]\n";
  }

  return ss.str();
}

std::string print_xformOps(const std::vector<XformOp> &xformOps,
                           const uint32_t indent) {
  std::stringstream ss;

  // To prevent printing xformOp attributes multiple times.
  std::set<std::string> printed_vars;

  // xforms props
  if (xformOps.size()) {
    for (size_t i = 0; i < xformOps.size(); i++) {
      const auto xformOp = xformOps[i];

      if (xformOp.op_type == XformOp::OpType::ResetXformStack) {
        // No need to print value.
        continue;
      }

      std::string varname = to_string(xformOp.op_type);
      if (!xformOp.suffix.empty()) {
        varname += ":" + xformOp.suffix;
      }

      DCOUT("has_default " << xformOp.has_default());
      DCOUT("has_timesamples " << xformOp.has_timesamples());

      if (xformOp.has_default()) {
        if (printed_vars.count(varname)) {
          continue;
        }

        printed_vars.insert(varname);

        ss << pprint::Indent(indent);
        ss << xformOp.get_value_type_name() << " ";
        ss << varname;
        ss << " = ";

        if (xformOp.is_blocked()) {
          ss << "None";
        } else if (auto pv = xformOp.get_scalar()) {
          ss << value::pprint_value(pv.value(), indent);
        } else {
          ss << "[InternalError]";
        }

        // TODO: metadata
        ss << "\n";
      }

      // Check if timeSamples were authored (even if empty)
      bool has_timesamples_authored = (xformOp.has_timesamples() || xformOp.get_var().ts_raw().type_id() != 0);

      if (has_timesamples_authored) {

        if (printed_vars.count(varname + ".timeSamples")) {
          continue;
        }

        printed_vars.insert(varname + ".timeSamples");

        ss << pprint::Indent(indent);
        ss << xformOp.get_value_type_name() << " ";
        ss << varname;
        ss << ".timeSamples";
        ss << " = ";

        // Always use ts_raw() to get timeSamples even if empty
        ss << print_timesamples(xformOp.get_var().ts_raw(), indent);
        ss << "\n";
      }

    }
  }

  // uniform token[] xformOpOrder
  ss << print_xformOpOrder(xformOps, indent);

  return ss.str();
}


std::string print_material_binding(const MaterialBinding *mb, const uint32_t indent) {
  if (!mb) {
    return std::string();
  }

  std::stringstream ss;

  if (mb->materialBinding) {
    ss << print_relationship(mb->materialBinding.value(),
                             mb->materialBinding.value().get_listedit_qual(),
                             /* custom */ false, kMaterialBinding, indent);
  }

  if (mb->materialBindingPreview) {
    ss << print_relationship(
        mb->materialBindingPreview.value(),
        mb->materialBindingPreview.value().get_listedit_qual(),
        /* custom */ false, kMaterialBindingPreview, indent);
  }

  if (mb->materialBindingFull) {
    ss << print_relationship(
        mb->materialBindingFull.value(),
        mb->materialBindingFull.value().get_listedit_qual(),
        /* custom */ false, kMaterialBindingFull, indent);
  }

  // NOTE: matb does not include "material:binding", "material:binding:preview" and "material:binding:full"
  for (const auto &matb : mb->materialBindingMap()) {
    if (matb.first.empty()) {
      // this should not happen
      continue;
    }

    std::string matb_name = kMaterialBinding + std::string(":") + matb.first;

    ss << print_relationship(
        matb.second,
        matb.second.get_listedit_qual(),
        /* custom */ false, matb_name, indent);
    
  }

  // TODO: sort by collection name?
  for (const auto &collection : mb->materialBindingCollectionMap()) {

    std::string purpose_name;
    if (!collection.first.empty()) {
      purpose_name = std::string(":") + collection.first;
    }

    for (size_t i = 0; i < collection.second.size(); i++) {
      std::string coll_name = collection.second.keys()[i];
 
      const Relationship *rel{nullptr};
      if (!collection.second.at(i, &rel)) {
        // this should not happen though.
        continue;
      }

      std::string rel_name;

      if (coll_name.empty()) { 
        rel_name = kMaterialBindingCollection + purpose_name;
      } else {
        rel_name = kMaterialBindingCollection + std::string(":") + coll_name + purpose_name;
      }

      ss << print_relationship(
          *rel,
          rel->get_listedit_qual(),
          /* custom */ false, rel_name, indent);
    }
  }

  return ss.str();
}

std::string print_collection(const Collection *coll, const uint32_t indent) {
  std::stringstream ss;

  if (!coll) {
    return std::string();
  }

  const auto &instances = coll->instances();

  for (size_t i = 0; i < instances.size(); i++) {
    std::string name = instances.keys()[i];

    CollectionInstance instance;
    if (!instances.at(i, &instance)) {
      continue;
    }

    std::string prefix = "collection";
    if (name.size()) {
      prefix += ":" + name;
    }

    if (instance.expansionRule.authored()) {
      ss << print_typed_token_attr(instance.expansionRule, prefix + ":expansionRule", indent);

    }


    if (instance.includeRoot.authored()) {
      ss << print_typed_attr(instance.includeRoot, prefix + ":includeRoot", indent);
    }

    if (instance.includes) {
      ss << print_relationship(
          instance.includes.value(),
          instance.includes.value().get_listedit_qual(),
          /* custom */ false, prefix + ":includes", indent);
        
    }

    if (instance.excludes) {
      ss << print_relationship(
          instance.excludes.value(),
          instance.excludes.value().get_listedit_qual(),
          /* custom */ false, prefix + ":excludes", indent);
        
    }
  }

  return ss.str();
}

template <typename T>
std::string print_gprim_predefined(const T &gprim, const uint32_t indent) {
  std::stringstream ss;

  // properties
  ss << print_typed_attr(gprim.doubleSided, "doubleSided", indent);
  ss << print_typed_token_attr(gprim.orientation, "orientation", indent);
  ss << print_typed_token_attr(gprim.purpose, "purpose", indent);
  ss << print_typed_attr(gprim.extent, "extent", indent);

  ss << print_typed_token_attr(gprim.visibility, "visibility", indent);

  ss << print_material_binding(&gprim, indent);

  ss << print_collection(&gprim, indent);

  if (gprim.proxyPrim.authored()) {
    const Relationship &rel = gprim.proxyPrim.relationship();
    ss << print_relationship(rel, rel.get_listedit_qual(), /* custom */ false,
                             "proxyPrim", indent);
  }

  ss << print_xformOps(gprim.xformOps, indent);

  return ss.str();
}


// Moved some 'to_string' to value-pprint.cc

std::string to_string(const APISchemas::APIName &name) {
  std::string s;

  switch (name) {
    case APISchemas::APIName::VisibilityAPI: {
      s = "VisibilityAPI";
      break;
    }
    case APISchemas::APIName::XformCommonAPI: {
      s = "XformCommonAPI";
      break;
    }
    case APISchemas::APIName::SkelBindingAPI: {
      s = "SkelBindingAPI";
      break;
    }
    case APISchemas::APIName::MotionAPI: {
      s = "MotionAPI";
      break;
    }
    case APISchemas::APIName::PrimvarsAPI: {
      s = "PrimvarsAPI";
      break;
    }
    case APISchemas::APIName::CollectionAPI: {
      s = "CollectionAPI";
      break;
    }
    case APISchemas::APIName::ConnectableAPI: {
      s = "ConnectableAPI";
      break;
    }
    case APISchemas::APIName::CoordSysAPI: {
      s = "CoordSysAPI";
      break;
    }
    case APISchemas::APIName::NodeDefAPI: {
      s = "NodeDefAPI";
      break;
    }
    case APISchemas::APIName::MaterialBindingAPI: {
      s = "MaterialBindingAPI";
      break;
    }
    case APISchemas::APIName::ShapingAPI: {
      s = "ShapingAPI";
      break;
    }
    case APISchemas::APIName::ShadowAPI: {
      s = "ShadowAPI";
      break;
    }
    case APISchemas::APIName::GeomModelAPI: {
      s = "GeomModelAPI";
      break;
    }
    case APISchemas::APIName::ListAPI: {
      s = "ListAPI";
      break;
    }
    case APISchemas::APIName::LightAPI: {
      s = "LightAPI";
      break;
    }
    case APISchemas::APIName::LightListAPI: {
      s = "LightListAPI";
      break;
    }
    case APISchemas::APIName::VolumeLightAPI: {
      s = "VolumeLightAPI";
      break;
    }
    case APISchemas::APIName::MeshLightAPI: {
      s = "MeshLightAPI";
      break;
    }
    case APISchemas::APIName::Preliminary_AnchoringAPI: {
      s = "Preliminary_AnchoringAPI";
      break;
    }
    case APISchemas::APIName::Preliminary_PhysicsColliderAPI: {
      s = "Preliminary_PhysicsColliderAPI";
      break;
    }
    case APISchemas::APIName::Preliminary_PhysicsRigidBodyAPI: {
      s = "Preliminary_PhysicsRigidBodyAPI";
      break;
    }
    case APISchemas::APIName::Preliminary_PhysicsMaterialAPI: {
      s = "Preliminary_PhysicsMaterialAPI";
      break;
    }
  }

  return s;
}

std::string to_string(const CustomDataType &custom) {
  return print_customData(custom, "", 0);
}


std::string to_string(const Reference &v) {
  std::stringstream ss;

  // For internal references (no asset path, just prim path), don't output "@@"
  if (!v.asset_path.GetAssetPath().empty()) {
    ss << v.asset_path;
  }
  if (v.prim_path.is_valid()) {
    ss << v.prim_path;
  }

  ss << v.layerOffset;

  if (!v.customData.empty()) {
    // TODO: Indent
    ss << print_customData(v.customData, "customData", /* indent */ 0);
  }

  return ss.str();
}

std::string to_string(const Payload &v) {
  std::stringstream ss;

  if (v.is_none()) {
    // pxrUSD serialize and prints 'None' for payload by filling all members in Payload empty.
    ss << "None";

  } else {
    // For internal payloads (no asset path, just prim path), don't output "@@"
    if (!v.asset_path.GetAssetPath().empty()) {
      ss << v.asset_path;
    }
    if (v.prim_path.is_valid()) {
      ss << v.prim_path;
    }

    ss << v.layerOffset;
  }

  return ss.str();
}

std::string print_variantSelectionMap(const VariantSelectionMap &m,
                                      const uint32_t indent) {
  std::stringstream ss;

  if (m.empty()) {
    return ss.str();
  }

  ss << pprint::Indent(indent) << "variants = {\n";
  for (const auto &item : m) {
    ss << pprint::Indent(indent + 1) << "string " << item.first << " = "
       << quote(item.second) << "\n";
  }
  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string print_customData(const CustomDataType &customData,
                             const std::string &dict_name,
                             const uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent);
  if (!dict_name.empty()) {
    std::string name = dict_name;

    if (!isValidIdentifier(name)) {
      // May contain "/", quote it
      name = quote(name);
    }

    ss << name << " = {\n";
  } else {
    ss << "{\n";
  }
  for (const auto &item : customData) {
    ss << print_meta(item.second, indent + 1, true, item.first);
  }
  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string print_meta(const MetaVariable &meta, const uint32_t indent, bool emit_type_name,
                       const std::string &varname) {
  std::stringstream ss;

  // ss << "TODO: isObject " << meta.is_object() << ", isValue " <<
  // meta.IsValue() << "\n";

  // Use varname if meta.name is empty
  std::string name = meta.get_name();
  if (name.empty()) {
    name = varname;
  }

  if (name.empty()) {
    name = "[ERROR:EmptyName]";
  }

  if (auto pv = meta.get_value<CustomDataType>()) {
    // dict
    if (!isValidIdentifier(name)) {
      // May contain "/", quote it
      name = quote(name);
    }
    ss << pprint::Indent(indent) << "dictionary " << name << " = {\n";
    for (const auto &item : pv.value()) {
      ss << print_meta(item.second, indent + 1, /* emit_type_name */true, item.first);
    }
    ss << pprint::Indent(indent) << "}\n";
  } else {
    ss << pprint::Indent(indent);
    if (emit_type_name) {
      ss << meta.type_name() << " ";
    }
    ss << name << " = "
       << pprint_value(meta.get_raw_value()) << "\n";
  }

  return ss.str();
}

std::string to_string(tinyusdz::GeomMesh::InterpolateBoundary v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::InterpolateBoundary::InterpolateBoundaryNone: {
      s = "none";
      break;
    }
    case tinyusdz::GeomMesh::InterpolateBoundary::EdgeAndCorner: {
      s = "edgeAndCorner";
      break;
    }
    case tinyusdz::GeomMesh::InterpolateBoundary::EdgeOnly: {
      s = "edgeOnly";
      break;
    }
  }

  return s;
}

std::string to_string(tinyusdz::GeomMesh::SubdivisionScheme v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::SubdivisionScheme::CatmullClark: {
      s = "catmullClark";
      break;
    }
    case tinyusdz::GeomMesh::SubdivisionScheme::Loop: {
      s = "loop";
      break;
    }
    case tinyusdz::GeomMesh::SubdivisionScheme::Bilinear: {
      s = "bilinear";
      break;
    }
    case tinyusdz::GeomMesh::SubdivisionScheme::SubdivisionSchemeNone: {
      s = "none";
      break;
    }
  }

  return s;
}

std::string to_string(tinyusdz::GeomMesh::FaceVaryingLinearInterpolation v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::CornersPlus1: {
      s = "cornersPlus1";
      break;
    }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::CornersPlus2: {
      s = "cornersPlus2";
      break;
    }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::CornersOnly: {
      s = "cornersOnly";
      break;
    }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::Boundaries: {
      s = "boundaries";
      break;
    }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::
        FaceVaryingLinearInterpolationNone: {
      s = "none";
      break;
    }
    case tinyusdz::GeomMesh::FaceVaryingLinearInterpolation::All: {
      s = "all";
      break;
    }
  }

  return s;
}

std::string to_string(tinyusdz::GeomSubset::ElementType v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomSubset::ElementType::Face: {
      s = "face";
      break;
    }
    case tinyusdz::GeomSubset::ElementType::Point: {
      s = "point";
      break;
    }
    case tinyusdz::GeomSubset::ElementType::Edge: {
      s = "edge";
      break;
    }
    case tinyusdz::GeomSubset::ElementType::Tetrahedron: {
      s = "tetrahedron";
      break;
    }
  }

  return s;
}

std::string to_string(tinyusdz::GeomSubset::FamilyType v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomSubset::FamilyType::Partition: {
      s = "partition";
      break;
    }
    case tinyusdz::GeomSubset::FamilyType::NonOverlapping: {
      s = "nonOverlapping";
      break;
    }
    case tinyusdz::GeomSubset::FamilyType::Unrestricted: {
      s = "unrestricted";
      break;
    }
  }

  return s;
}

std::string to_string(tinyusdz::CollectionInstance::ExpansionRule rule) {
  std::string s;

  switch (rule) {
    case tinyusdz::CollectionInstance::ExpansionRule::ExplicitOnly: {
      s = kExplicitOnly;
      break;
    }
    case tinyusdz::CollectionInstance::ExpansionRule::ExpandPrims: {
      s = kExpandPrims;
      break;
    }
    case tinyusdz::CollectionInstance::ExpansionRule::ExpandPrimsAndProperties: {
      s = kExpandPrimsAndProperties;
      break;
    }
  }

  return s;
}

std::string to_string(const tinyusdz::UsdPreviewSurface::OpacityMode v) {
  std::string s;

  switch (v) {
    case tinyusdz::UsdPreviewSurface::OpacityMode::Opacity: {
      s = "opacity";
      break;
    }
    case tinyusdz::UsdPreviewSurface::OpacityMode::Transparent: {
      s = "transparent";
      break;
    }
    case tinyusdz::UsdPreviewSurface::OpacityMode::Presence: {
      s = "presence";
      break;
    }
  }

  return s;
}

std::string to_string(const tinyusdz::UsdUVTexture::SourceColorSpace v) {
  std::string s;

  switch (v) {
    case tinyusdz::UsdUVTexture::SourceColorSpace::Auto: {
      s = "auto";
      break;
    }
    case tinyusdz::UsdUVTexture::SourceColorSpace::Raw: {
      s = "raw";
      break;
    }
    case tinyusdz::UsdUVTexture::SourceColorSpace::SRGB: {
      s = "sRGB";
      break;
    }
  }

  return s;
}

std::string to_string(const tinyusdz::UsdUVTexture::Wrap v) {
  std::string s;

  switch (v) {
    case tinyusdz::UsdUVTexture::Wrap::UseMetadata: {
      s = "useMetadata";
      break;
    }
    case tinyusdz::UsdUVTexture::Wrap::Black: {
      s = "black";
      break;
    }
    case tinyusdz::UsdUVTexture::Wrap::Clamp: {
      s = "clamp";
      break;
    }
    case tinyusdz::UsdUVTexture::Wrap::Repeat: {
      s = "repeat";
      break;
    }
    case tinyusdz::UsdUVTexture::Wrap::Mirror: {
      s = "mirror";
      break;
    }
  }

  return s;
}

std::string to_string(tinyusdz::Kind v) {
  if (v == tinyusdz::Kind::Model) {
    return "model";
  } else if (v == tinyusdz::Kind::Group) {
    return "group";
  } else if (v == tinyusdz::Kind::Assembly) {
    return "assembly";
  } else if (v == tinyusdz::Kind::Component) {
    return "component";
  } else if (v == tinyusdz::Kind::Subcomponent) {
    return "subcomponent";
  } else if (v == tinyusdz::Kind::SceneLibrary) {
    return "sceneLibrary";
  } else if (v == tinyusdz::Kind::UserDef) {
    // Should use PrimMeta::get_kind() to get actual Kind string value.
    return "[[InternalError. UserDefKind]]";
  } else {
    return "[[InvalidKind]]";
  }
}

std::string to_string(tinyusdz::Axis v) {
  if (v == tinyusdz::Axis::X) {
    return "X";
  } else if (v == tinyusdz::Axis::Y) {
    return "Y";
  } else if (v == tinyusdz::Axis::Z) {
    return "Z";
  } else {
    return "[[InvalidAxis]]";
  }
}

std::string to_string(tinyusdz::Visibility v) {
  if (v == tinyusdz::Visibility::Inherited) {
    return "inherited";
  } else {
    return "invisible";
  }
}

std::string to_string(tinyusdz::Orientation o) {
  if (o == tinyusdz::Orientation::RightHanded) {
    return "rightHanded";
  } else {
    return "leftHanded";
  }
}

std::string to_string(tinyusdz::ListEditQual v) {
  if (v == tinyusdz::ListEditQual::ResetToExplicit) {
    return "";  // unqualified
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "append";
  } else if (v == tinyusdz::ListEditQual::Add) {
    return "add";
  } else if (v == tinyusdz::ListEditQual::Delete) {
    return "delete";
  } else if (v == tinyusdz::ListEditQual::Prepend) {
    return "prepend";
  } else if (v == tinyusdz::ListEditQual::Order) {
    return "order";
  }

  return "[[Invalid ListEditQual value]]";
}

std::string to_string(tinyusdz::Interpolation interp) {
  switch (interp) {
    case Interpolation::Invalid:
      return "[[Invalid interpolation value]]";
    case Interpolation::Constant:
      return "constant";
    case Interpolation::Uniform:
      return "uniform";
    case Interpolation::Varying:
      return "varying";
    case Interpolation::Vertex:
      return "vertex";
    case Interpolation::FaceVarying:
      return "faceVarying";
  }

  // Never reach here though
  return "[[Invalid interpolation value]]";
}

std::string to_string(tinyusdz::SpecType ty) {
  if (SpecType::Attribute == ty) {
    return "SpecTypeAttribute";
  } else if (SpecType::Connection == ty) {
    return "SpecTypeConnection";
  } else if (SpecType::Expression == ty) {
    return "SpecTypeExpression";
  } else if (SpecType::Mapper == ty) {
    return "SpecTypeMapper";
  } else if (SpecType::MapperArg == ty) {
    return "SpecTypeMapperArg";
  } else if (SpecType::Prim == ty) {
    return "SpecTypePrim";
  } else if (SpecType::PseudoRoot == ty) {
    return "SpecTypePseudoRoot";
  } else if (SpecType::Relationship == ty) {
    return "SpecTypeRelationship";
  } else if (SpecType::RelationshipTarget == ty) {
    return "SpecTypeRelationshipTarget";
  } else if (SpecType::Variant == ty) {
    return "SpecTypeVariant";
  } else if (SpecType::VariantSet == ty) {
    return "SpecTypeVariantSet";
  }
  return "SpecTypeInvalid";
}

std::string to_string(tinyusdz::Specifier s) {
  if (s == tinyusdz::Specifier::Def) {
    return "def";
  } else if (s == tinyusdz::Specifier::Over) {
    return "over";
  } else if (s == tinyusdz::Specifier::Class) {
    return "class";
  } else {
    return "[[SpecifierInvalid]]";
  }
}

std::string to_string(tinyusdz::Permission s) {
  if (s == tinyusdz::Permission::Public) {
    return "public";
  } else if (s == tinyusdz::Permission::Private) {
    return "private";
  } else {
    return "[[PermissionInvalid]]";
  }
}

std::string to_string(tinyusdz::Purpose purpose) {
  switch (purpose) {
    case Purpose::Default: {
      return "default";
    }
    case Purpose::Render: {
      return "render";
    }
    case Purpose::Guide: {
      return "guide";
    }
    case Purpose::Proxy: {
      return "proxy";
    }
  }

  // Never reach here though
  return "[[Invalid Purpose value]]";
}

std::string to_string(tinyusdz::Variability v) {
  if (v == tinyusdz::Variability::Varying) {
    return "varying";
  } else if (v == tinyusdz::Variability::Uniform) {
    return "uniform";
  } else if (v == tinyusdz::Variability::Config) {
    return "config";
  } else {
    return "\"[[VariabilityInvalid]]\"";
  }
}

std::string to_string(tinyusdz::Extent e) {
  std::stringstream ss;

  ss << "[" << e.lower << ", " << e.upper << "]";

  return ss.str();
}

// Forward declaration — print_prim is defined below in namespace prim
namespace prim { std::string print_prim(const Prim &prim, const uint32_t indent); }

std::string print_variantSetStmt(
    const std::map<std::string, VariantSet> &vslist, const uint32_t indent) {
  std::stringstream ss;

  for (const auto &variantSet : vslist) {
    if (variantSet.second.variantSet.empty()) {
      continue;
    }

    ss << pprint::Indent(indent) << "variantSet " << quote(variantSet.first)
       << " = {\n";

    for (const auto &item : variantSet.second.variantSet) {
      ss << pprint::Indent(indent + 1) << quote(item.first) << " ";

      if (item.second.metas().authored()) {
        ss << "(\n";
        ss << print_prim_metas(item.second.metas(), indent + 2);
        ss << pprint::Indent(indent + 1) << ") ";
      }

      ss << "{\n";

      // props
      ss << print_props(item.second.properties(), indent + 2);

      // primChildren
      // TODO: print child Prims based on `primChildren` Prim metadata
      const auto &variantPrimMetas = item.second.metas();
      const auto &variantPrimChildren = item.second.primChildren();

      if (variantPrimMetas.primChildren.size() == variantPrimChildren.size()) {
        std::map<std::string, const Prim *> primNameTable;
        for (size_t i = 0; i < variantPrimChildren.size(); i++) {
          primNameTable.emplace(variantPrimChildren[i].element_name(),
                                &variantPrimChildren[i]);
        }

        for (size_t i = 0; i < variantPrimMetas.primChildren.size(); i++) {
          value::token nameTok = variantPrimMetas.primChildren[i];
          DCOUT(fmt::format("variantPrimChildren  {}/{} = {}", i,
                            variantPrimMetas.primChildren.size(),
                            nameTok.str()));
          const auto it = primNameTable.find(nameTok.str());
          if (it != primNameTable.end()) {
            ss << prim::print_prim(*(it->second), indent + 2);
          } else {
            // TODO: Report warning?
          }
        }
      } else {
        for (const auto &child : variantPrimChildren) {
          ss << prim::print_prim(child, indent + 2);
        }
      }

      // nested variantSets
      if (item.second.variantSets().size()) {
        ss << print_variantSetStmt(item.second.variantSets(), indent+2);
      }

      ss << pprint::Indent(indent+1) << "}\n";

    }

    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string print_variantSetSpecStmt(
    const std::map<std::string, VariantSetSpec> &vslist,
    const uint32_t indent) {
  std::stringstream ss;

  // ss << "# variantSet.size = " << std::to_string(vslist.size()) << "\n";
  for (const auto &variantSet : vslist) {
    if (variantSet.second.variantSet.empty()) {
      continue;
    }

    ss << pprint::Indent(indent) << "variantSet " << quote(variantSet.first)
       << " = {\n";

    for (const auto &item : variantSet.second.variantSet) {
      ss << pprint::Indent(indent + 1) << quote(item.first) << " ";

      if (item.second.metas().authored()) {
        ss << "(\n";
        ss << print_prim_metas(item.second.metas(), indent + 2);
        ss << pprint::Indent(indent + 1) << ") ";
      }

      ss << "{\n";

      // props
      ss << print_props(item.second.props(), indent + 2);

      // primChildren
      // TODO: print child Prims based on `primChildren` Prim metadata
      const auto &variantPrimMetas = item.second.metas();
      const auto &variantPrimChildren = item.second.children();

      if (variantPrimMetas.primChildren.size() == variantPrimChildren.size()) {
        std::map<std::string, const PrimSpec *> primNameTable;
        for (size_t i = 0; i < variantPrimChildren.size(); i++) {
          primNameTable.emplace(variantPrimChildren[i].name(),
                                &variantPrimChildren[i]);
        }

        for (size_t i = 0; i < variantPrimMetas.primChildren.size(); i++) {
          value::token nameTok = variantPrimMetas.primChildren[i];
          DCOUT(fmt::format("variantPrimChildren  {}/{} = {}", i,
                            variantPrimMetas.primChildren.size(),
                            nameTok.str()));
          const auto it = primNameTable.find(nameTok.str());
          if (it != primNameTable.end()) {
            ss << prim::print_primspec(*(it->second), indent + 2);
          } else {
            // TODO: Report warning?
          }
        }
      } else {
        for (const auto &child : variantPrimChildren) {
          ss << prim::print_primspec(child, indent + 2);
        }
      }

      // ss << "# variantSet end\n";
      ss << pprint::Indent(indent + 1) << "}\n";
    }

    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Model &model, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(model.spec);
  if (model.prim_type_name.size()) {
    ss << " " << model.prim_type_name;
  }
  ss << " \"" << model.name << "\"\n";

  if (model.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(model.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  std::set<std::string> tokset;
  ss << print_props(model.props, tokset, model.propertyNames(), indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Scope &scope, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(scope.spec) << " Scope \""
     << scope.name << "\"\n";
  if (scope.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(scope.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  std::set<std::string> tokset;
  ss << print_props(scope.props, tokset, scope.propertyNames(), indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GPrim &gprim, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(gprim.spec) << " GPrim \""
     << gprim.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_gprim_predefined(gprim, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Xform &xform, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(xform.spec) << " Xform \""
     << xform.name << "\"\n";
  if (xform.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(xform.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_gprim_predefined(xform, indent + 1);

  ss << print_props(xform.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCamera &camera, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(camera.spec) << " Camera \""
     << camera.name << "\"\n";
  if (camera.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(camera.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(camera.clippingRange, "clippingRange", indent + 1);
  ss << print_typed_attr(camera.clippingPlanes, "clippingPlanes", indent + 1);
  ss << print_typed_attr(camera.focalLength, "focalLength", indent + 1);
  ss << print_typed_attr(camera.horizontalAperture, "horizontalAperture",
                         indent + 1);
  ss << print_typed_attr(camera.horizontalApertureOffset,
                         "horizontalApertureOffset", indent + 1);
  ss << print_typed_attr(camera.verticalAperture, "verticalAperture",
                         indent + 1);
  ss << print_typed_attr(camera.verticalApertureOffset,
                         "verticalApertureOffset", indent + 1);

  ss << print_typed_attr(camera.exposure, "exposure", indent + 1);
  ss << print_typed_attr(camera.focusDistance, "focusDistance", indent + 1);
  ss << print_typed_attr(camera.fStop, "fStop", indent + 1);

  ss << print_typed_token_attr(camera.projection, "projection", indent + 1);
  ss << print_typed_token_attr(camera.stereoRole, "stereoRole", indent + 1);

  ss << print_typed_attr(camera.shutterOpen, "shutter:open", indent + 1);
  ss << print_typed_attr(camera.shutterClose, "shutter:close", indent + 1);

  ss << print_gprim_predefined(camera, indent + 1);

  ss << print_props(camera.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomSphere &sphere, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(sphere.spec) << " Sphere \""
     << sphere.name << "\"\n";
  if (sphere.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(sphere.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(sphere.radius, "radius", indent + 1);

  ss << print_gprim_predefined(sphere, indent + 1);

  ss << print_props(sphere.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomMesh &mesh, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(mesh.spec) << " Mesh \""
     << mesh.name << "\"\n";
  if (mesh.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(mesh.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(mesh.points, "points", indent + 1);
  ss << print_typed_attr(mesh.normals, "normals", indent + 1);
  ss << print_typed_attr(mesh.faceVertexIndices, "faceVertexIndices",
                         indent + 1);
  ss << print_typed_attr(mesh.faceVertexCounts, "faceVertexCounts", indent + 1);

  if (mesh.skeleton) {
    ss << print_relationship(mesh.skeleton.value(),
                             mesh.skeleton.value().get_listedit_qual(),
                             /* custom */ false, "skel:skeleton", indent + 1);
  }

  ss << print_typed_attr(mesh.blendShapes, "skel:blendShapes", indent + 1);
  if (mesh.blendShapeTargets) {
    ss << print_relationship(mesh.blendShapeTargets.value(),
                             mesh.blendShapeTargets.value().get_listedit_qual(),
                             /* custom */ false, "skel:blendShapeTargets",
                             indent + 1);
  }

  for (const auto &item : mesh.subsetFamilyTypeMap) {
    std::string attr_name = "subsetFamily:" + item.first.str() + ":familyType";
    // TODO: Support connection attr?
    ss << pprint::Indent(indent+1) << "uniform token " << attr_name << " = " << quote(to_string(item.second)) << "\n";
  }

  // subdiv
  ss << print_typed_attr(mesh.cornerIndices, "cornerIndices", indent + 1);
  ss << print_typed_attr(mesh.cornerSharpnesses, "cornerSharpnesses",
                         indent + 1);
  ss << print_typed_attr(mesh.creaseIndices, "creaseIndices", indent + 1);
  ss << print_typed_attr(mesh.creaseLengths, "creaseLengths", indent + 1);
  ss << print_typed_attr(mesh.creaseSharpnesses, "creaseSharpnesses",
                         indent + 1);
  ss << print_typed_attr(mesh.holeIndices, "holeIndices", indent + 1);

  ss << print_typed_token_attr(mesh.subdivisionScheme, "subdivisionScheme",
                               indent + 1);
  ss << print_typed_token_attr(mesh.interpolateBoundary, "interpolateBoundary",
                               indent + 1);
  ss << print_typed_token_attr(mesh.faceVaryingLinearInterpolation,
                               "faceVaryingLinearInterpolation", indent + 1);

  ss << print_gprim_predefined(mesh, indent + 1);


  ss << print_props(mesh.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomSubset &subset, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(subset.spec) << " GeomSubset \""
     << subset.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(subset.meta, indent + 1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_token_attr(subset.elementType, "elementType", indent + 1);
  ss << print_typed_attr(subset.familyName, "familyName", indent + 1);
  ss << print_typed_attr(subset.indices, "indices", indent + 1);

  ss << print_material_binding(&subset, indent + 1);
  ss << print_collection(&subset, indent + 1);

  ss << print_props(subset.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomPoints &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " Points \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.points, "points", indent + 1);
  ss << print_typed_attr(geom.normals, "normals", indent + 1);
  ss << print_typed_attr(geom.widths, "widths", indent + 1);
  ss << print_typed_attr(geom.ids, "ids", indent + 1);
  ss << print_typed_attr(geom.velocities, "velocities", indent + 1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);

  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomBasisCurves::Type &ty) {
  std::string s;

  switch (ty) {
    case GeomBasisCurves::Type::Cubic: {
      s = "cubic";
      break;
    }
    case GeomBasisCurves::Type::Linear: {
      s = "linear";
      break;
    }
  }

  return s;
}

std::string to_string(const GeomBasisCurves::Basis &ty) {
  std::string s;

  switch (ty) {
    case GeomBasisCurves::Basis::Bezier: {
      s = "bezier";
      break;
    }
    case GeomBasisCurves::Basis::Bspline: {
      s = "bspline";
      break;
    }
    case GeomBasisCurves::Basis::CatmullRom: {
      s = "catmullRom";
      break;
    }
  }

  return s;
}

std::string to_string(const GeomBasisCurves::Wrap &ty) {
  std::string s;

  switch (ty) {
    case GeomBasisCurves::Wrap::Nonperiodic: {
      s = "nonperiodic";
      break;
    }
    case GeomBasisCurves::Wrap::Periodic: {
      s = "periodic";
      break;
    }
    case GeomBasisCurves::Wrap::Pinned: {
      s = "pinned";
      break;
    }
  }

  return s;
}

std::string to_string(const GeomBasisCurves &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " BasisCurves \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_token_attr(geom.type, "type", indent + 1);
  ss << print_typed_token_attr(geom.basis, "basis", indent + 1);
  ss << print_typed_token_attr(geom.wrap, "wrap", indent + 1);

  ss << print_typed_attr(geom.points, "points", indent + 1);
  ss << print_typed_attr(geom.normals, "normals", indent + 1);
  ss << print_typed_attr(geom.widths, "widths", indent + 1);
  ss << print_typed_attr(geom.velocities, "velocities", indent + 1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent + 1);
  ss << print_typed_attr(geom.curveVertexCounts, "curveVertexCounts",
                         indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);

  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomNurbsCurves &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " NurbsCurves \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.points, "points", indent + 1);
  ss << print_typed_attr(geom.normals, "normals", indent + 1);
  ss << print_typed_attr(geom.widths, "widths", indent + 1);
  ss << print_typed_attr(geom.velocities, "velocities", indent + 1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent + 1);
  ss << print_typed_attr(geom.curveVertexCounts, "curveVertexCounts",
                         indent + 1);

  //
  ss << print_typed_attr(geom.order, "order", indent + 1);
  ss << print_typed_attr(geom.knots, "knots", indent + 1);
  ss << print_typed_attr(geom.ranges, "ranges", indent + 1);
  ss << print_typed_attr(geom.pointWeights, "pointWeights", indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);

  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCube &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " Cube \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.size, "size", indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);

  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCone &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " Cone \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.radius, "radius", indent + 1);
  ss << print_typed_attr(geom.height, "height", indent + 1);

  if (geom.axis.authored()) {
    std::string axis;
    if (geom.axis.get_value() == Axis::X) {
      axis = "\"X\"";
    } else if (geom.axis.get_value() == Axis::Y) {
      axis = "\"Y\"";
    } else {
      axis = "\"Z\"";
    }
    ss << pprint::Indent(indent + 1) << "uniform token axis = " << axis << "\n";
  }

  ss << print_gprim_predefined(geom, indent + 1);
  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCylinder &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " Cylinder \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.radius, "radius", indent + 1);
  ss << print_typed_attr(geom.height, "height", indent + 1);

  if (geom.axis.authored()) {
    std::string axis;
    if (geom.axis.get_value() == Axis::X) {
      axis = "\"X\"";
    } else if (geom.axis.get_value() == Axis::Y) {
      axis = "\"Y\"";
    } else {
      axis = "\"Z\"";
    }
    ss << pprint::Indent(indent + 1) << "uniform token axis = " << axis << "\n";
  }

  ss << print_gprim_predefined(geom, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCapsule &geom, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " Capsule \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.radius, "radius", indent + 1);
  ss << print_typed_attr(geom.height, "height", indent + 1);

  if (geom.axis.authored()) {
    std::string axis;
    if (geom.axis.get_value() == Axis::X) {
      axis = "\"X\"";
    } else if (geom.axis.get_value() == Axis::Y) {
      axis = "\"Y\"";
    } else {
      axis = "\"Z\"";
    }
    ss << pprint::Indent(indent + 1) << "uniform token axis = " << axis << "\n";
  }

  ss << print_gprim_predefined(geom, indent + 1);
  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomPointInstancer &instancer, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(instancer.spec)
     << " PointInstancer \"" << instancer.name << "\"\n";
  if (instancer.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(instancer.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  if (instancer.prototypes) {
    ss << print_relationship(instancer.prototypes.value(),
                             instancer.prototypes.value().get_listedit_qual(),
                             /* custom */ false, "prototypes", indent + 1);
  }
  ss << print_typed_attr(instancer.protoIndices, "protoIndices", indent + 1);
  ss << print_typed_attr(instancer.ids, "ids", indent + 1);
  ss << print_typed_attr(instancer.invisibleIds, "invisibleIds", indent + 1);
  ss << print_typed_attr(instancer.inactiveIds, "inactiveIds", indent + 1);
  ss << print_typed_attr(instancer.positions, "positions", indent + 1);
  ss << print_typed_attr(instancer.orientations, "orientations", indent + 1);
  ss << print_typed_attr(instancer.scales, "scales", indent + 1);
  ss << print_typed_attr(instancer.velocities, "velocities", indent + 1);
  ss << print_typed_attr(instancer.accelerations, "accelerations", indent + 1);
  ss << print_typed_attr(instancer.angularVelocities, "angularVelocities",
                         indent + 1);

  ss << print_gprim_predefined(instancer, indent + 1);

  ss << print_props(instancer.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SkelRoot &root, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(root.spec) << " SkelRoot \""
     << root.name << "\"\n";
  if (root.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(root.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_token_attr(root.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(root.purpose, "purpose", indent + 1);
  ss << print_typed_attr(root.extent, "extent", indent + 1);

  if (root.proxyPrim) {
    ss << print_relationship(root.proxyPrim.value(),
                             root.proxyPrim.value().get_listedit_qual(),
                             /* custom */ false, "proxyPrim", indent + 1);
  }

  if (root.animationSource) {
    ss << print_relationship(root.animationSource.value(),
                             root.animationSource.value().get_listedit_qual(),
                             /* custom */ false, "skel:animationSource",
                             indent + 1);
  }

  if (root.skeleton) {
    ss << print_relationship(root.skeleton.value(),
                             root.skeleton.value().get_listedit_qual(),
                             /* custom */ false, "skel:skeleton", indent + 1);
  }

  ss << print_xformOps(root.xformOps, indent + 1);

  ss << print_props(root.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Skeleton &skel, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(skel.spec) << " Skeleton \""
     << skel.name << "\"\n";
  if (skel.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(skel.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(skel.bindTransforms, "bindTransforms", indent + 1);
  ss << print_typed_attr(skel.jointNames, "jointNames", indent + 1);
  ss << print_typed_attr(skel.joints, "joints", indent + 1);
  ss << print_typed_attr(skel.restTransforms, "restTransforms", indent + 1);

  if (skel.animationSource) {
    ss << print_relationship(skel.animationSource.value(),
                             skel.animationSource.value().get_listedit_qual(),
                             /* custom */ false, "skel:animationSource",
                             indent + 1);
  }

  if (skel.proxyPrim) {
    ss << print_relationship(skel.proxyPrim.value(),
                             skel.proxyPrim.value().get_listedit_qual(),
                             /* custom */ false, "proxyPrim", indent + 1);
  }

  ss << print_xformOps(skel.xformOps, indent + 1);

  ss << print_typed_token_attr(skel.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(skel.purpose, "purpose", indent + 1);
  ss << print_typed_attr(skel.extent, "extent", indent + 1);

  ss << print_props(skel.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SkelAnimation &skelanim, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(skelanim.spec)
     << " SkelAnimation \"" << skelanim.name << "\"\n";
  if (skelanim.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(skelanim.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(skelanim.blendShapes, "blendShapes", indent + 1);
  ss << print_typed_attr(skelanim.blendShapeWeights, "blendShapeWeights",
                         indent + 1);
  ss << print_typed_attr(skelanim.joints, "joints", indent + 1);
  ss << print_typed_attr(skelanim.rotations, "rotations", indent + 1);
  ss << print_typed_attr(skelanim.scales, "scales", indent + 1);
  ss << print_typed_attr(skelanim.translations, "translations", indent + 1);

  ss << print_props(skelanim.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const BlendShape &prim, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(prim.spec) << " BlendShape \""
     << prim.name << "\"\n";
  if (prim.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(prim.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(prim.offsets, "offsets", indent + 1);
  ss << print_typed_attr(prim.normalOffsets, "normalOffsets", indent + 1);
  ss << print_typed_attr(prim.pointIndices, "pointIndices", indent + 1);

  ss << print_props(prim.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Material &material, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(material.spec) << " Material \""
     << material.name << "\"\n";
  if (material.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(material.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  if (material.surface.authored()) {
    // assume connection when authored.
    // TODO: list edit?.
    ss << pprint::Indent(indent + 1) << "token outputs:surface.connect ";

    const auto &conns = material.surface.get_connections();
    if (conns.size() == 1) {
      ss << "= " << pquote(conns[0]);
    } else if (conns.size() > 1) {
      ss << "= [";
      for (size_t i = 0; i < conns.size(); i++) {
        ss << pquote(conns[i]);
        if (i != (conns.size() - 1)) {
          ss << ", ";
        }
      }
      ss << "]";
    }

    if (material.surface.metas().authored()) {
      ss << "(\n"
         << print_attr_metas(material.surface.metas(), indent + 2)
         << pprint::Indent(indent + 1) << ")";
    }
    ss << "\n";
  }

  if (material.displacement.authored()) {
    // assume connection when authored.
    // TODO: list edit?.
    ss << pprint::Indent(indent + 1) << "token outputs:displacement.connect ";

    const auto &conns = material.displacement.get_connections();
    if (conns.size() == 1) {
      ss << "= " << pquote(conns[0]);
    } else if (conns.size() > 1) {
      ss << "= [";
      for (size_t i = 0; i < conns.size(); i++) {
        ss << pquote(conns[i]);
        if (i != (conns.size() - 1)) {
          ss << ", ";
        }
      }
      ss << "]";
    }

    if (material.displacement.metas().authored()) {
      ss << "(\n"
         << print_attr_metas(material.displacement.metas(), indent + 2)
         << pprint::Indent(indent + 1) << ")";
    }
    ss << "\n";
  }

  if (material.volume.authored()) {
    // assume connection when authored.
    // TODO: list edit?.
    ss << pprint::Indent(indent + 1) << "token outputs:volume.connect ";

    const auto &conns = material.volume.get_connections();
    if (conns.size() == 1) {
      ss << "= " << pquote(conns[0]);
    } else if (conns.size() > 1) {
      ss << "= [";
      for (size_t i = 0; i < conns.size(); i++) {
        ss << pquote(conns[i]);
        if (i != (conns.size() - 1)) {
          ss << ", ";
        }
      }
      ss << "]";
    }

    if (material.volume.metas().authored()) {
      ss << "(\n"
         << print_attr_metas(material.volume.metas(), indent + 2)
         << pprint::Indent(indent + 1) << ")";
    }
    ss << "\n";
  }

  // Print MaterialXConfigAPI attributes if present
  if (material.materialXConfig) {
    const auto &mtlxConfig = material.materialXConfig.value();
    if (mtlxConfig.mtlx_version.authored()) {
      ss << pprint::Indent(indent + 1) << "string config:mtlx:version = "
         << quote(mtlxConfig.mtlx_version.get_value()) << "\n";
    }
    if (mtlxConfig.mtlx_namespace.authored()) {
      ss << pprint::Indent(indent + 1) << "string config:mtlx:namespace = "
         << quote(mtlxConfig.mtlx_namespace.get_value()) << "\n";
    }
    if (mtlxConfig.mtlx_colorspace.authored()) {
      ss << pprint::Indent(indent + 1) << "string config:mtlx:colorspace = "
         << quote(mtlxConfig.mtlx_colorspace.get_value()) << "\n";
    }
    if (mtlxConfig.mtlx_sourceUri.authored()) {
      ss << pprint::Indent(indent + 1) << "asset config:mtlx:sourceUri = "
         << mtlxConfig.mtlx_sourceUri.get_value() << "\n";
    }
  }

  ss << print_props(material.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

static std::string print_common_shader_params(const ShaderNode &shader,
                                              const uint32_t indent) {
  std::stringstream ss;

  ss << print_props(shader.props, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float2 &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float3 &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_float4 &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_string &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_normal &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_vector &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_point &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPrimvarReader_matrix &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_str_attr(shader.varname, "inputs:varname", indent);
  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdTransform2d &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.in, "inputs:in", indent);
  ss << print_typed_attr(shader.rotation, "inputs:rotation", indent);
  ss << print_typed_attr(shader.scale, "inputs:scale", indent);
  ss << print_typed_attr(shader.translation, "inputs:translation", indent);
  ss << print_typed_terminal_attr(shader.result, "outputs:result", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdPreviewSurface &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.diffuseColor, "inputs:diffuseColor", indent);
  ss << print_typed_attr(shader.emissiveColor, "inputs:emissiveColor", indent);
  ss << print_typed_attr(shader.useSpecularWorkflow,
                         "inputs:useSpecularWorkflow", indent);
  ss << print_typed_attr(shader.ior, "inputs:ior", indent);
  ss << print_typed_attr(shader.specularColor, "inputs:specularColor", indent);
  ss << print_typed_attr(shader.metallic, "inputs:metallic", indent);
  ss << print_typed_attr(shader.clearcoat, "inputs:clearcoat", indent);
  ss << print_typed_attr(shader.clearcoatRoughness, "inputs:clearcoatRoughness",
                         indent);
  ss << print_typed_attr(shader.roughness, "inputs:roughness", indent);
  ss << print_typed_attr(shader.opacity, "inputs:opacity", indent);
  ss << print_typed_token_attr(shader.opacityMode, "inputs:opacityMode",
                         indent);
  ss << print_typed_attr(shader.opacityThreshold, "inputs:opacityThreshold",
                         indent);
  ss << print_typed_attr(shader.normal, "inputs:normal", indent);
  ss << print_typed_attr(shader.displacement, "inputs:displacement", indent);
  ss << print_typed_attr(shader.occlusion, "inputs:occlusion", indent);

  ss << print_typed_terminal_attr(shader.outputsSurface, "outputs:surface",
                                  indent);
  ss << print_typed_terminal_attr(shader.outputsDisplacement,
                                  "outputs:displacement", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const UsdUVTexture &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  ss << print_typed_attr(shader.file, "inputs:file", indent);

  ss << print_typed_token_attr(shader.sourceColorSpace,
                               "inputs:sourceColorSpace", indent);

  ss << print_typed_attr(shader.fallback, "inputs:fallback", indent);

  ss << print_typed_attr(shader.bias, "inputs:bias", indent);
  ss << print_typed_attr(shader.scale, "inputs:scale", indent);

  ss << print_typed_attr(shader.st, "inputs:st", indent);
  ss << print_typed_token_attr(shader.wrapS, "inputs:wrapT", indent);
  ss << print_typed_token_attr(shader.wrapT, "inputs:wrapS", indent);

  ss << print_typed_terminal_attr(shader.outputsR, "outputs:r", indent);
  ss << print_typed_terminal_attr(shader.outputsG, "outputs:g", indent);
  ss << print_typed_terminal_attr(shader.outputsB, "outputs:b", indent);
  ss << print_typed_terminal_attr(shader.outputsA, "outputs:a", indent);
  ss << print_typed_terminal_attr(shader.outputsRGB, "outputs:rgb", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const MtlxOpenPBRSurface &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  // Base properties
  ss << print_typed_attr(shader.base_weight, "inputs:base_weight", indent);
  ss << print_typed_attr(shader.base_color, "inputs:base_color", indent);
  ss << print_typed_attr(shader.base_metalness, "inputs:base_metalness", indent);
  ss << print_typed_attr(shader.base_diffuse_roughness, "inputs:base_diffuse_roughness", indent);

  // Specular properties
  ss << print_typed_attr(shader.specular_weight, "inputs:specular_weight", indent);
  ss << print_typed_attr(shader.specular_color, "inputs:specular_color", indent);
  ss << print_typed_attr(shader.specular_roughness, "inputs:specular_roughness", indent);
  ss << print_typed_attr(shader.specular_ior, "inputs:specular_ior", indent);
  ss << print_typed_attr(shader.specular_anisotropy, "inputs:specular_anisotropy", indent);
  ss << print_typed_attr(shader.specular_rotation, "inputs:specular_rotation", indent);
  ss << print_typed_attr(shader.specular_roughness_anisotropy, "inputs:specular_roughness_anisotropy", indent);

  // Transmission properties
  ss << print_typed_attr(shader.transmission_weight, "inputs:transmission_weight", indent);
  ss << print_typed_attr(shader.transmission_color, "inputs:transmission_color", indent);
  ss << print_typed_attr(shader.transmission_depth, "inputs:transmission_depth", indent);
  ss << print_typed_attr(shader.transmission_scatter, "inputs:transmission_scatter", indent);
  ss << print_typed_attr(shader.transmission_scatter_anisotropy, "inputs:transmission_scatter_anisotropy", indent);
  ss << print_typed_attr(shader.transmission_dispersion, "inputs:transmission_dispersion", indent);
  ss << print_typed_attr(shader.transmission_dispersion_abbe_number, "inputs:transmission_dispersion_abbe_number", indent);
  ss << print_typed_attr(shader.transmission_dispersion_scale, "inputs:transmission_dispersion_scale", indent);

  // Subsurface properties
  ss << print_typed_attr(shader.subsurface_weight, "inputs:subsurface_weight", indent);
  ss << print_typed_attr(shader.subsurface_color, "inputs:subsurface_color", indent);
  ss << print_typed_attr(shader.subsurface_radius, "inputs:subsurface_radius", indent);
  ss << print_typed_attr(shader.subsurface_radius_scale, "inputs:subsurface_radius_scale", indent);
  ss << print_typed_attr(shader.subsurface_scale, "inputs:subsurface_scale", indent);
  ss << print_typed_attr(shader.subsurface_anisotropy, "inputs:subsurface_anisotropy", indent);
  ss << print_typed_attr(shader.subsurface_scatter_anisotropy, "inputs:subsurface_scatter_anisotropy", indent);

  // Coat properties
  ss << print_typed_attr(shader.coat_weight, "inputs:coat_weight", indent);
  ss << print_typed_attr(shader.coat_color, "inputs:coat_color", indent);
  ss << print_typed_attr(shader.coat_roughness, "inputs:coat_roughness", indent);
  ss << print_typed_attr(shader.coat_anisotropy, "inputs:coat_anisotropy", indent);
  ss << print_typed_attr(shader.coat_rotation, "inputs:coat_rotation", indent);
  ss << print_typed_attr(shader.coat_roughness_anisotropy, "inputs:coat_roughness_anisotropy", indent);
  ss << print_typed_attr(shader.coat_ior, "inputs:coat_ior", indent);
  ss << print_typed_attr(shader.coat_darkening, "inputs:coat_darkening", indent);
  ss << print_typed_attr(shader.coat_affect_color, "inputs:coat_affect_color", indent);
  ss << print_typed_attr(shader.coat_affect_roughness, "inputs:coat_affect_roughness", indent);

  // Fuzz properties
  ss << print_typed_attr(shader.fuzz_weight, "inputs:fuzz_weight", indent);
  ss << print_typed_attr(shader.fuzz_color, "inputs:fuzz_color", indent);
  ss << print_typed_attr(shader.fuzz_roughness, "inputs:fuzz_roughness", indent);

  // Thin film properties
  ss << print_typed_attr(shader.thin_film_thickness, "inputs:thin_film_thickness", indent);
  ss << print_typed_attr(shader.thin_film_ior, "inputs:thin_film_ior", indent);
  ss << print_typed_attr(shader.thin_film_weight, "inputs:thin_film_weight", indent);

  // Emission properties
  ss << print_typed_attr(shader.emission_luminance, "inputs:emission_luminance", indent);
  ss << print_typed_attr(shader.emission_color, "inputs:emission_color", indent);

  // Geometry properties
  ss << print_typed_attr(shader.geometry_opacity, "inputs:geometry_opacity", indent);
  ss << print_typed_attr(shader.geometry_thin_walled, "inputs:geometry_thin_walled", indent);
  ss << print_typed_attr(shader.geometry_normal, "inputs:geometry_normal", indent);
  ss << print_typed_attr(shader.geometry_tangent, "inputs:geometry_tangent", indent);
  ss << print_typed_attr(shader.geometry_coat_normal, "inputs:geometry_coat_normal", indent);
  ss << print_typed_attr(shader.geometry_coat_tangent, "inputs:geometry_coat_tangent", indent);

  // Output
  ss << print_typed_terminal_attr(shader.surface, "outputs:surface", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

static std::string print_shader_params(const OpenPBRSurface &shader,
                                       const uint32_t indent) {
  std::stringstream ss;

  // Base properties
  ss << print_typed_attr(shader.base_weight, "inputs:base_weight", indent);
  ss << print_typed_attr(shader.base_color, "inputs:base_color", indent);
  ss << print_typed_attr(shader.base_roughness, "inputs:base_roughness", indent);
  ss << print_typed_attr(shader.base_metalness, "inputs:base_metalness", indent);
  ss << print_typed_attr(shader.base_diffuse_roughness, "inputs:base_diffuse_roughness", indent);

  // Specular properties
  ss << print_typed_attr(shader.specular_weight, "inputs:specular_weight", indent);
  ss << print_typed_attr(shader.specular_color, "inputs:specular_color", indent);
  ss << print_typed_attr(shader.specular_roughness, "inputs:specular_roughness", indent);
  ss << print_typed_attr(shader.specular_ior, "inputs:specular_ior", indent);
  ss << print_typed_attr(shader.specular_ior_level, "inputs:specular_ior_level", indent);
  ss << print_typed_attr(shader.specular_anisotropy, "inputs:specular_anisotropy", indent);
  ss << print_typed_attr(shader.specular_rotation, "inputs:specular_rotation", indent);

  // Transmission properties
  ss << print_typed_attr(shader.transmission_weight, "inputs:transmission_weight", indent);
  ss << print_typed_attr(shader.transmission_color, "inputs:transmission_color", indent);
  ss << print_typed_attr(shader.transmission_depth, "inputs:transmission_depth", indent);
  ss << print_typed_attr(shader.transmission_scatter, "inputs:transmission_scatter", indent);
  ss << print_typed_attr(shader.transmission_scatter_anisotropy, "inputs:transmission_scatter_anisotropy", indent);
  ss << print_typed_attr(shader.transmission_dispersion, "inputs:transmission_dispersion", indent);

  // Subsurface properties
  ss << print_typed_attr(shader.subsurface_weight, "inputs:subsurface_weight", indent);
  ss << print_typed_attr(shader.subsurface_color, "inputs:subsurface_color", indent);
  ss << print_typed_attr(shader.subsurface_radius, "inputs:subsurface_radius", indent);
  ss << print_typed_attr(shader.subsurface_radius_scale, "inputs:subsurface_radius_scale", indent);
  ss << print_typed_attr(shader.subsurface_scale, "inputs:subsurface_scale", indent);
  ss << print_typed_attr(shader.subsurface_anisotropy, "inputs:subsurface_anisotropy", indent);

  // Sheen properties
  ss << print_typed_attr(shader.sheen_weight, "inputs:sheen_weight", indent);
  ss << print_typed_attr(shader.sheen_color, "inputs:sheen_color", indent);
  ss << print_typed_attr(shader.sheen_roughness, "inputs:sheen_roughness", indent);

  // Fuzz properties
  ss << print_typed_attr(shader.fuzz_weight, "inputs:fuzz_weight", indent);
  ss << print_typed_attr(shader.fuzz_color, "inputs:fuzz_color", indent);
  ss << print_typed_attr(shader.fuzz_roughness, "inputs:fuzz_roughness", indent);

  // Thin film properties
  ss << print_typed_attr(shader.thin_film_weight, "inputs:thin_film_weight", indent);
  ss << print_typed_attr(shader.thin_film_thickness, "inputs:thin_film_thickness", indent);
  ss << print_typed_attr(shader.thin_film_ior, "inputs:thin_film_ior", indent);

  // Coat properties
  ss << print_typed_attr(shader.coat_weight, "inputs:coat_weight", indent);
  ss << print_typed_attr(shader.coat_color, "inputs:coat_color", indent);
  ss << print_typed_attr(shader.coat_roughness, "inputs:coat_roughness", indent);
  ss << print_typed_attr(shader.coat_anisotropy, "inputs:coat_anisotropy", indent);
  ss << print_typed_attr(shader.coat_rotation, "inputs:coat_rotation", indent);
  ss << print_typed_attr(shader.coat_ior, "inputs:coat_ior", indent);
  ss << print_typed_attr(shader.coat_affect_color, "inputs:coat_affect_color", indent);
  ss << print_typed_attr(shader.coat_affect_roughness, "inputs:coat_affect_roughness", indent);

  // Emission properties
  ss << print_typed_attr(shader.emission_luminance, "inputs:emission_luminance", indent);
  ss << print_typed_attr(shader.emission_color, "inputs:emission_color", indent);

  // Geometry properties
  ss << print_typed_attr(shader.opacity, "inputs:opacity", indent);
  ss << print_typed_attr(shader.normal, "inputs:normal", indent);
  ss << print_typed_attr(shader.tangent, "inputs:tangent", indent);

  // Output
  ss << print_typed_terminal_attr(shader.surface, "outputs:surface", indent);

  ss << print_common_shader_params(shader, indent);

  return ss.str();
}

std::string to_string(const Shader &shader, const uint32_t indent,
                      bool closing_brace) {
  // generic Shader class
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(shader.spec) << " Shader \""
     << shader.name << "\"\n";
  if (shader.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(shader.metas(), indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  if (shader.info_id.size()) {
    ss << pprint::Indent(indent + 1) << "uniform token info:id = \""
       << shader.info_id << "\"\n";
  }

  if (auto pvr = shader.value.get_value<UsdPrimvarReader_float>()) {
    ss << print_shader_params(pvr.value(), indent + 1);
  } else if (auto pvr2 = shader.value.get_value<UsdPrimvarReader_float2>()) {
    ss << print_shader_params(pvr2.value(), indent + 1);
  } else if (auto pvr3 = shader.value.get_value<UsdPrimvarReader_float3>()) {
    ss << print_shader_params(pvr3.value(), indent + 1);
  } else if (auto pvr4 = shader.value.get_value<UsdPrimvarReader_float4>()) {
    ss << print_shader_params(pvr4.value(), indent + 1);
  } else if (auto pvrs = shader.value.get_value<UsdPrimvarReader_string>()) {
    ss << print_shader_params(pvrs.value(), indent + 1);
  } else if (auto pvrn = shader.value.get_value<UsdPrimvarReader_normal>()) {
    ss << print_shader_params(pvrn.value(), indent + 1);
  } else if (auto pvrv = shader.value.get_value<UsdPrimvarReader_vector>()) {
    ss << print_shader_params(pvrv.value(), indent + 1);
  } else if (auto pvrp = shader.value.get_value<UsdPrimvarReader_point>()) {
    ss << print_shader_params(pvrp.value(), indent + 1);
  } else if (auto pvrm = shader.value.get_value<UsdPrimvarReader_matrix>()) {
    ss << print_shader_params(pvrm.value(), indent + 1);
  } else if (auto pvtex = shader.value.get_value<UsdUVTexture>()) {
    ss << print_shader_params(pvtex.value(), indent + 1);
  } else if (auto pvtx2d = shader.value.get_value<UsdTransform2d>()) {
    ss << print_shader_params(pvtx2d.value(), indent + 1);
  } else if (auto pvs = shader.value.get_value<UsdPreviewSurface>()) {
    ss << print_shader_params(pvs.value(), indent + 1);
  } else if (auto mtlx_opbr = shader.value.get_value<MtlxOpenPBRSurface>()) {
    // Blender v4.5 MaterialX OpenPBR Surface
    ss << print_shader_params(mtlx_opbr.value(), indent + 1);
  } else if (auto opbr = shader.value.get_value<OpenPBRSurface>()) {
    // Native OpenPBR Surface shader
    ss << print_shader_params(opbr.value(), indent + 1);
  } else if (auto pvsn = shader.value.get_value<ShaderNode>()) {
    // Generic ShaderNode
    ss << print_common_shader_params(pvsn.value(), indent + 1);
  } else {
    ss << pprint::Indent(indent + 1)
       << "[???] Invalid ShaderNode in Shader Prim\n";
  }

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const NodeGraph &nodegraph, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(nodegraph.spec) << " NodeGraph \""
     << nodegraph.name << "\"\n";
  if (nodegraph.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(nodegraph.metas(), indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // NodeGraph-specific attributes
  if (nodegraph.nodedef.authored()) {
    ss << print_typed_attr(nodegraph.nodedef, "nodedef", indent + 1);
  }

  if (nodegraph.nodegraph_type.authored()) {
    ss << print_typed_attr(nodegraph.nodegraph_type, "nodegraph_type", indent + 1);
  }

  // Print properties (inputs, outputs, etc.)
  ss << print_props(nodegraph.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const UsdPreviewSurface &surf, const uint32_t indent,
                      bool closing_brace) {
  // TODO: Print spec and meta?
  std::stringstream ss;

  ss << pprint::Indent(indent) << "{\n";
  ss << print_shader_params(surf, indent);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const UsdUVTexture &tex, const uint32_t indent,
                      bool closing_brace) {
  // TODO: Print spec and meta?
  std::stringstream ss;

  ss << pprint::Indent(indent) << "{\n";
  ss << print_shader_params(tex, indent);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const UsdPrimvarReader_float2 &preader,
                      const uint32_t indent, bool closing_brace) {
  // TODO: Print spec and meta?
  std::stringstream ss;

  ss << pprint::Indent(indent) << "{\n";
  ss << print_shader_params(preader, indent);
  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SphereLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " SphereLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  ss << print_typed_attr(light.radius, "inputs:radius", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  // ShapingAPI attributes
  ss << print_typed_attr(light.shapingConeAngle, "inputs:shaping:cone:angle", indent + 1);
  ss << print_typed_attr(light.shapingConeSoftness, "inputs:shaping:cone:softness", indent + 1);
  ss << print_typed_attr(light.shapingFocus, "inputs:shaping:focus", indent + 1);
  ss << print_typed_attr(light.shapingFocusTint, "inputs:shaping:focusTint", indent + 1);

  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const DistantLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " DistantLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  ss << print_typed_attr(light.angle, "inputs:angle", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  //ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const CylinderLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " CylinderLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  ss << print_typed_attr(light.length, "inputs:length", indent + 1);
  ss << print_typed_attr(light.radius, "inputs:radius", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  // ShapingAPI attributes
  ss << print_typed_attr(light.shapingConeAngle, "inputs:shaping:cone:angle", indent + 1);
  ss << print_typed_attr(light.shapingConeSoftness, "inputs:shaping:cone:softness", indent + 1);
  ss << print_typed_attr(light.shapingFocus, "inputs:shaping:focus", indent + 1);
  ss << print_typed_attr(light.shapingFocusTint, "inputs:shaping:focusTint", indent + 1);

  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const DiskLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " DiskLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  ss << print_typed_attr(light.radius, "inputs:radius", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  // ShapingAPI attributes
  ss << print_typed_attr(light.shapingConeAngle, "inputs:shaping:cone:angle", indent + 1);
  ss << print_typed_attr(light.shapingConeSoftness, "inputs:shaping:cone:softness", indent + 1);
  ss << print_typed_attr(light.shapingFocus, "inputs:shaping:focus", indent + 1);
  ss << print_typed_attr(light.shapingFocusTint, "inputs:shaping:focusTint", indent + 1);

  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const DomeLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " DomeLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  ss << print_typed_attr(light.guideRadius, "guideRadius", indent + 1);
  ss << print_typed_attr(light.file, "inputs:texture:file", indent + 1);
  ss << print_typed_token_attr(light.textureFormat, "inputs:texture:format",
                               indent + 1);

  //ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);

  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const RectLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " RectLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  ss << print_typed_attr(light.file, "inputs:texture:file", indent + 1);
  ss << print_typed_attr(light.height, "inputs:height", indent + 1);
  ss << print_typed_attr(light.width, "inputs:width", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  // ShapingAPI attributes
  ss << print_typed_attr(light.shapingConeAngle, "inputs:shaping:cone:angle", indent + 1);
  ss << print_typed_attr(light.shapingConeSoftness, "inputs:shaping:cone:softness", indent + 1);
  ss << print_typed_attr(light.shapingFocus, "inputs:shaping:focus", indent + 1);
  ss << print_typed_attr(light.shapingFocusTint, "inputs:shaping:focusTint", indent + 1);

  ss << print_typed_attr(light.extent, "extent", indent + 1);
  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeometryLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " GeometryLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const PortalLight &light, const uint32_t indent,
                      bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(light.spec) << " PortalLight \""
     << light.name << "\"\n";
  if (light.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(light.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(light.color, "inputs:color", indent + 1);
  ss << print_typed_attr(light.colorTemperature, "inputs:colorTemperature",
                         indent + 1);
  ss << print_typed_attr(light.diffuse, "inputs:diffuse", indent + 1);
  ss << print_typed_attr(light.enableColorTemperature,
                         "inputs:enableColorTemperature", indent + 1);
  ss << print_typed_attr(light.exposure, "inputs:exposure", indent + 1);
  ss << print_typed_attr(light.intensity, "inputs:intensity", indent + 1);
  ss << print_typed_attr(light.normalize, "inputs:normalize", indent + 1);
  ss << print_typed_attr(light.specular, "inputs:specular", indent + 1);

  // ShadowAPI attributes
  ss << print_typed_attr(light.shadowColor, "inputs:shadow:color", indent + 1);
  ss << print_typed_attr(light.shadowDistance, "inputs:shadow:distance", indent + 1);
  ss << print_typed_attr(light.shadowEnable, "inputs:shadow:enable", indent + 1);
  ss << print_typed_attr(light.shadowFalloff, "inputs:shadow:falloff", indent + 1);
  ss << print_typed_attr(light.shadowFalloffGamma, "inputs:shadow:falloffGamma", indent + 1);

  ss << print_typed_token_attr(light.visibility, "visibility", indent + 1);
  ss << print_typed_token_attr(light.purpose, "purpose", indent + 1);

  ss << print_xformOps(light.xformOps, indent + 1);
  ss << print_props(light.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCamera::Projection &proj) {
  if (proj == GeomCamera::Projection::Orthographic) {
    return "orthographic";
  } else {
    return "perspective";
  }
}

std::string to_string(const GeomCamera::StereoRole &role) {
  if (role == GeomCamera::StereoRole::Mono) {
    return "mono";
  } else if (role == GeomCamera::StereoRole::Right) {
    return "right";
  } else {
    return "left";
  }
}

std::string to_string(const Path &path, bool show_full_path) {
  if (show_full_path) {
    return path.full_path_name();
  } else {
    // TODO
    return path.full_path_name();
  }
}

std::string to_string(const std::vector<Path> &v, bool show_full_path) {
  // TODO(syoyo): indent
  std::stringstream ss;
  ss << "[";

  for (size_t i = 0; i < v.size(); i++) {
    ss << to_string(v[i], show_full_path);
    if (i != (v.size() - 1)) {
      ss << ", ";
    }
  }
  ss << "]";
  return ss.str();
}

std::string to_string(const XformOp::OpType &op) {
  std::string ss;

  switch (op) {
    case XformOp::OpType::ResetXformStack: {
      ss = "!resetXformStack!";
      break;
    }
    case XformOp::OpType::Transform: {
      ss = "xformOp:transform";
      break;
    }
    case XformOp::OpType::Translate: {
      ss = "xformOp:translate";
      break;
    }
    case XformOp::OpType::Scale: {
      ss = "xformOp:scale";
      break;
    }
    case XformOp::OpType::RotateX: {
      ss = "xformOp:rotateX";
      break;
    }
    case XformOp::OpType::RotateY: {
      ss = "xformOp:rotateY";
      break;
    }
    case XformOp::OpType::RotateZ: {
      ss = "xformOp:rotateZ";
      break;
    }
    case XformOp::OpType::RotateXYZ: {
      ss = "xformOp:rotateXYZ";
      break;
    }
    case XformOp::OpType::RotateXZY: {
      ss = "xformOp:rotateXZY";
      break;
    }
    case XformOp::OpType::RotateYXZ: {
      ss = "xformOp:rotateYXZ";
      break;
    }
    case XformOp::OpType::RotateYZX: {
      ss = "xformOp:rotateYZX";
      break;
    }
    case XformOp::OpType::RotateZXY: {
      ss = "xformOp:rotateZXY";
      break;
    }
    case XformOp::OpType::RotateZYX: {
      ss = "xformOp:rotateZYX";
      break;
    }
    case XformOp::OpType::Orient: {
      ss = "xformOp:orient";
      break;
    }
  }

  return ss;
}

//std::string to_string(const tinyusdz::value::token &v) { return v.str(); }

// to_string(DomeLight::TextureFormat) is defined in usdLux.cc

std::string dump_path(const Path &path) {
  std::stringstream ss;
  ss << "Path: Prim part = " << path.prim_part();
  ss << ", Prop part = " << path.prop_part();
  ss << ", Variant part = " << path.variant_part();
  ss << ", elementName = " << path.element_name();
  ss << ", isValid = " << path.is_valid();
  ss << ", isAbsolute = " << path.is_absolute_path();
  ss << ", isRelative = " << path.is_relative_path();

  return ss.str();
}

std::string print_layer_metas(const LayerMetas &metas, const uint32_t indent) {
  std::stringstream meta_ss;

  if (metas.doc.value.empty()) {
    // ss << pprint::Indent(1) << "doc = \"Exporterd from TinyUSDZ v" <<
    // tinyusdz::version_major
    //    << "." << tinyusdz::version_minor << "." << tinyusdz::version_micro
    //    << tinyusdz::version_rev << "\"\n";
  } else {
    meta_ss << pprint::Indent(indent) << "doc = " << to_string(metas.doc)
            << "\n";
  }

  if (metas.metersPerUnit.authored()) {
    meta_ss << pprint::Indent(indent)
            << "metersPerUnit = " << metas.metersPerUnit.get_value() << "\n";
  }

  if (metas.kilogramsPerUnit.authored()) {
    meta_ss << pprint::Indent(indent)
            << "kilogramsPerUnit = " << metas.kilogramsPerUnit.get_value() << "\n";
  }

  if (metas.upAxis.authored()) {
    meta_ss << pprint::Indent(indent)
            << "upAxis = " << quote(to_string(metas.upAxis.get_value()))
            << "\n";
  }

  if (metas.timeCodesPerSecond.authored()) {
    meta_ss << pprint::Indent(indent)
            << "timeCodesPerSecond = " << metas.timeCodesPerSecond.get_value()
            << "\n";
  }

  if (metas.startTimeCode.authored()) {
    meta_ss << pprint::Indent(indent)
            << "startTimeCode = " << metas.startTimeCode.get_value() << "\n";
  }

  if (metas.endTimeCode.authored()) {
    meta_ss << pprint::Indent(indent)
            << "endTimeCode = " << metas.endTimeCode.get_value() << "\n";
  }

  if (metas.framesPerSecond.authored()) {
    meta_ss << pprint::Indent(indent)
            << "framesPerSecond = " << metas.framesPerSecond.get_value()
            << "\n";
  }

  // TODO: Do not print subLayers when consumed(after composition evaluated)
  if (metas.subLayers.size()) {
    meta_ss << pprint::Indent(indent) << "subLayers = " << metas.subLayers
            << "\n";
  }

  if (metas.defaultPrim.str().size()) {
    meta_ss << pprint::Indent(1)
            << "defaultPrim = " << tinyusdz::quote(metas.defaultPrim.str())
            << "\n";
  }

  if (metas.autoPlay.authored()) {
    meta_ss << pprint::Indent(1)
            << "autoPlay = " << to_string(metas.autoPlay.get_value()) << "\n";
  }

  if (metas.playbackMode.authored()) {
    auto v = metas.playbackMode.get_value();
    if (v == LayerMetas::PlaybackMode::PlaybackModeLoop) {
      meta_ss << pprint::Indent(indent) << "playbackMode = \"loop\"\n";
    } else {  // None
      meta_ss << pprint::Indent(indent) << "playbackMode = \"none\"\n";
    }
  }

  if (!metas.comment.value.empty()) {
    // Stage meta omits 'comment'
    meta_ss << pprint::Indent(indent) << to_string(metas.comment) << "\n";
  }

  if (metas.customLayerData.size()) {
    meta_ss << print_customData(metas.customLayerData, "customLayerData",
                                /* indent */ 1);
  } else if (metas.customLayerDataAuthored) {
    // Print empty customLayerData if explicitly authored (match pxrUSD behavior)
    meta_ss << pprint::Indent(1) << "customLayerData = {\n";
    meta_ss << pprint::Indent(1) << "}\n";
  }

  return meta_ss.str();
}

std::string print_layer(const Layer &layer, const uint32_t indent, bool parallel) {
#if !defined(TINYUSDZ_ENABLE_THREAD)
  (void)parallel; // Threading disabled
#endif

  std::stringstream ss;

  // FIXME: print magic-header outside of this function?
  ss << pprint::Indent(indent) << "#usda 1.0\n";

  std::stringstream meta_ss;
  meta_ss << print_layer_metas(layer.metas(), indent + 1);

  if (meta_ss.str().size()) {
    ss << "(\n";
    ss << meta_ss.str();
    ss << ")\n";
  }

  ss << "\n";

  if (layer.metas().primChildren.size() == layer.primspecs().size()) {
    std::map<std::string, const PrimSpec *> primNameTable;
    for (const auto &item : layer.primspecs()) {
      primNameTable.emplace(item.first, &item.second);
    }

#if defined(TINYUSDZ_ENABLE_THREAD)
    if (parallel) {
      // Parallel printing path
      std::vector<const PrimSpec*> ordered_primspecs;
      ordered_primspecs.reserve(layer.metas().primChildren.size());

      for (size_t i = 0; i < layer.metas().primChildren.size(); i++) {
        value::token nameTok = layer.metas().primChildren[i];
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          ordered_primspecs.push_back(it->second);
        }
      }

      prim::ParallelPrintConfig config;
      ss << prim::print_primspecs_parallel(ordered_primspecs, indent, config);
    } else
#endif  // TINYUSDZ_ENABLE_THREAD
    {
      // Sequential printing path (original)
      for (size_t i = 0; i < layer.metas().primChildren.size(); i++) {
        value::token nameTok = layer.metas().primChildren[i];
        // DCOUT(fmt::format("primChildren  {}/{} = {}", i,
        //                   layer.metas().primChildren.size(), nameTok.str()));
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          ss << prim::print_primspec((*it->second), indent);
          if (i != (layer.metas().primChildren.size() - 1)) {
            ss << "\n";
          }
        } else {
          // TODO: Report warning?
        }
      }
    }
  } else {
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (parallel) {
      // Parallel printing path
      std::vector<const PrimSpec*> primspecs;
      primspecs.reserve(layer.primspecs().size());
      for (const auto &item : layer.primspecs()) {
        primspecs.push_back(&item.second);
      }

      prim::ParallelPrintConfig config;
      ss << prim::print_primspecs_parallel(primspecs, indent, config);
    } else
#endif  // TINYUSDZ_ENABLE_THREAD
    {
      // Sequential printing path (original)
      size_t i = 0;
      for (const auto &item : layer.primspecs()) {
        ss << prim::print_primspec(item.second, indent);
        if (i != (layer.primspecs().size() - 1)) {
          ss << "\n";
        }
        i++;
      }
    }
  }

  return ss.str();
}

// prim-pprint.hh
namespace prim {

std::string print_prim(const Prim &prim, const uint32_t indent) {
  std::stringstream ss;

  // Currently, Prim's elementName is read from name variable in concrete Prim
  // class(e.g. Xform::name).
  // TODO: use prim.elementPath for elementName.
  std::string s = pprint_value(prim.data(), indent, /* closing_brace */ false);

  bool require_newline = true;

  // Check last 2 chars.
  // if it ends with '{\n', no properties are authored so do not emit blank line
  // before printing VariantSet or child Prims.
  if (s.size() > 2) {
    if ((s[s.size() - 2] == '{') && (s[s.size() - 1] == '\n')) {
      require_newline = false;
    }
  }

  ss << s;

  //
  // print variant
  // TODO: Use print_variantSetStmt()
  //
  if (prim.variantSets().size()) {
    if (require_newline) {
      ss << "\n";
    }

    ss << print_variantSetStmt(prim.variantSets(), indent + 1);

    // need to add blank line after VariantSet stmt and before child Prims,
    // so set require_newline true
    require_newline = true;
  }

  //
  // primChildren
  //
  if (prim.children().size()) {
    if (require_newline) {
      ss << "\n";
      require_newline = false;
    }
    if (prim.metas().primChildren.size() == prim.children().size()) {
      // Use primChildren info to determine the order of the traversal.

      std::map<std::string, const Prim *> primNameTable;
      for (size_t i = 0; i < prim.children().size(); i++) {
        primNameTable.emplace(prim.children()[i].element_name(),
                              &prim.children()[i]);
      }

      for (size_t i = 0; i < prim.metas().primChildren.size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        value::token nameTok = prim.metas().primChildren[i];
        DCOUT(fmt::format("primChildren  {}/{} = {}", i,
                          prim.metas().primChildren.size(), nameTok.str()));
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          ss << print_prim(*(it->second), indent + 1);
        } else {
          // TODO: Report warning?
        }
      }

    } else {
      for (size_t i = 0; i < prim.children().size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        ss << print_prim(prim.children()[i], indent + 1);
      }
    }
  }

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

std::string print_primspec(const PrimSpec &primspec, const uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(primspec.specifier()) << " ";
  if (primspec.typeName().empty() || primspec.typeName() == "Model") {
    // do not emit typeName
  } else {
    ss << primspec.typeName() << " ";
  }

  ss << "\"" << primspec.name() << "\"\n";

  if (primspec.metas().authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(primspec.metas(), indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_props(primspec.props(), indent + 1);

  // TODO: print according to primChildren metadatum
  for (size_t i = 0; i < primspec.children().size(); i++) {
    if (i > 0) {
      ss << pprint::Indent(indent) << "\n";
    }
    ss << print_primspec(primspec.children()[i], indent + 1);
  }

  // ss << "# variant \n";
  ss << print_variantSetSpecStmt(primspec.variantSets(), indent + 1);

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

// ============================================================================
// StreamWriter overloads for efficient printing
// ============================================================================

void print_prim(StreamWriter& writer, const Prim &prim, const uint32_t indent) {
  // For now, delegate to string version
  // TODO: In future, refactor to write directly to StreamWriter
  writer.write(print_prim(prim, indent));
}

void print_primspec(StreamWriter& writer, const PrimSpec &primspec, const uint32_t indent) {
  // For now, delegate to string version
  // TODO: In future, refactor to write directly to StreamWriter
  writer.write(print_primspec(primspec, indent));
}

// ============================================================================
// ChunkedStreamWriter template implementations
// ============================================================================

template <size_t ChunkSize, size_t Alignment>
void print_prim(ChunkedStreamWriter<ChunkSize, Alignment>& writer, const Prim &prim, const uint32_t indent) {
  // Write directly to ChunkedStreamWriter for better performance
  // This avoids creating one giant string in memory

  // Currently, Prim's elementName is read from name variable in concrete Prim
  // class(e.g. Xform::name).
  // TODO: use prim.elementPath for elementName.
  std::string s = pprint_value(prim.data(), indent, /* closing_brace */ false);

  bool require_newline = true;

  // Check last 2 chars.
  // if it ends with '{\n', no properties are authored so do not emit blank line
  // before printing VariantSet or child Prims.
  if (s.size() > 2) {
    if ((s[s.size() - 2] == '{') && (s[s.size() - 1] == '\n')) {
      require_newline = false;
    }
  }

  writer.write(s);

  //
  // print variant
  //
  if (prim.variantSets().size()) {
    if (require_newline) {
      writer.write("\n");
    }

    // need to add blank line after VariantSet stmt and before child Prims,
    // so set require_newline true
    require_newline = true;

    for (const auto &variantSet : prim.variantSets()) {
      writer.write(pprint::Indent(indent + 1));
      writer.write("variantSet ");
      writer.write(quote(variantSet.first));
      writer.write(" = {\n");

      for (const auto &variantItem : variantSet.second.variantSet) {
        writer.write(pprint::Indent(indent + 2));
        writer.write(quote(variantItem.first));

        const Variant &variant = variantItem.second;

        if (variant.metas().authored()) {
          writer.write(" (\n");
          writer.write(print_prim_metas(variant.metas(), indent + 3));
          writer.write(pprint::Indent(indent + 2));
          writer.write(")");
        }

        writer.write(" {\n");

        writer.write(print_props(variant.properties(), indent + 3));

        if (variant.metas().variantChildren.has_value() &&
            (variant.metas().variantChildren.value().size() ==
             variant.primChildren().size())) {
          std::map<std::string, const Prim *> primNameTable;
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            primNameTable.emplace(variant.primChildren()[i].element_name(),
                                  &variant.primChildren()[i]);
          }

          for (size_t i = 0; i < variant.metas().variantChildren.value().size();
               i++) {
            value::token nameTok = variant.metas().variantChildren.value()[i];
            const auto it = primNameTable.find(nameTok.str());
            if (it != primNameTable.end()) {
              print_prim(writer, *(it->second), indent + 3);
              if (i != (variant.primChildren().size() - 1)) {
                writer.write("\n");
              }
            } else {
              // TODO: Report warning?
            }
          }

        } else {
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            print_prim(writer, variant.primChildren()[i], indent + 3);
            if (i != (variant.primChildren().size() - 1)) {
              writer.write("\n");
            }
          }
        }

        writer.write(pprint::Indent(indent + 2));
        writer.write("}\n");
      }

      writer.write(pprint::Indent(indent + 1));
      writer.write("}\n");
    }
  }

  //
  // primChildren
  //
  if (prim.children().size()) {
    if (require_newline) {
      writer.write("\n");
      require_newline = false;
    }
    if (prim.metas().primChildren.size() == prim.children().size()) {
      // Use primChildren info to determine the order of the traversal.

      std::map<std::string, const Prim *> primNameTable;
      for (size_t i = 0; i < prim.children().size(); i++) {
        primNameTable.emplace(prim.children()[i].element_name(),
                              &prim.children()[i]);
      }

      for (size_t i = 0; i < prim.metas().primChildren.size(); i++) {
        if (i > 0) {
          writer.write("\n");
        }
        value::token nameTok = prim.metas().primChildren[i];
        DCOUT(fmt::format("primChildren  {}/{} = {}", i,
                          prim.metas().primChildren.size(), nameTok.str()));
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          print_prim(writer, *(it->second), indent + 1);
        } else {
          // TODO: Report warning?
        }
      }

    } else {
      for (size_t i = 0; i < prim.children().size(); i++) {
        if (i > 0) {
          writer.write("\n");
        }
        print_prim(writer, prim.children()[i], indent + 1);
      }
    }
  }

  writer.write(pprint::Indent(indent));
  writer.write("}\n");
}

template <size_t ChunkSize, size_t Alignment>
void print_primspec(ChunkedStreamWriter<ChunkSize, Alignment>& writer, const PrimSpec &primspec, const uint32_t indent) {
  // Write directly to ChunkedStreamWriter for better performance
  writer.write(pprint::Indent(indent));
  writer.write(to_string(primspec.specifier()));
  writer.write(" ");

  if (primspec.typeName().empty() || primspec.typeName() == "Model") {
    // do not emit typeName
  } else {
    writer.write(primspec.typeName());
    writer.write(" ");
  }

  writer.write("\"");
  writer.write(primspec.name());
  writer.write("\"\n");

  if (primspec.metas().authored()) {
    writer.write(pprint::Indent(indent));
    writer.write("(\n");
    writer.write(print_prim_metas(primspec.metas(), indent + 1));
    writer.write(pprint::Indent(indent));
    writer.write(")\n");
  }
  writer.write(pprint::Indent(indent));
  writer.write("{\n");

  writer.write(print_props(primspec.props(), indent + 1));

  // TODO: print according to primChildren metadatum
  for (size_t i = 0; i < primspec.children().size(); i++) {
    if (i > 0) {
      writer.write(pprint::Indent(indent));
      writer.write("\n");
    }
    print_primspec(writer, primspec.children()[i], indent + 1);
  }

  // writer.write("# variant \n");
  writer.write(print_variantSetSpecStmt(primspec.variantSets(), indent + 1));

  writer.write(pprint::Indent(indent));
  writer.write("}\n");
}

// Explicit template instantiations for common parameters
template void print_prim<4096, 16>(ChunkedStreamWriter<4096, 16>& writer, const Prim &prim, const uint32_t indent);
template void print_primspec<4096, 16>(ChunkedStreamWriter<4096, 16>& writer, const PrimSpec &primspec, const uint32_t indent);

}  // namespace prim

std::string to_string(const Layer &layer, const uint32_t indent,
                      bool closing_brace) {
  (void)closing_brace;
  return print_layer(layer, indent);
}

std::string to_string(const PrimSpec &primspec, const uint32_t indent,
                      bool closing_brace) {
  (void)closing_brace;
  return prim::print_primspec(primspec, indent);
}

}  // namespace tinyusdz
