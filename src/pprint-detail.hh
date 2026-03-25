// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Internal-only header: typed attribute printing templates and helpers.
// Included by pprint-meta.cc, pprint-geom.cc, pprint-shader.cc, etc.
// Not a public API header.
//
#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "pprint-enum.hh"    // for to_string, pprint::Indent
#include "value-pprint.hh"   // for operator<<, pprint_value
#include "str-util.hh"       // for quote, buildEscapedAndQuotedStringForUSDA

#include "common-macros.inc"

namespace tinyusdz {

// Forward declarations — defined in pprint-meta.cc
std::string print_attr_metas(const AttrMeta &meta, const uint32_t indent);
std::string print_prim_metas(const PrimMeta &meta, const uint32_t indent);
std::string print_xformOps(const std::vector<XformOp> &xformOps,
                           const uint32_t indent);
std::string print_material_binding(const MaterialBinding *mb,
                                   const uint32_t indent);
std::string print_collection(const Collection *coll, const uint32_t indent);
std::string print_props(const std::map<std::string, Property> &props,
                        uint32_t indent);

// ============================================================================
// Small inline helpers
// ============================================================================

// Helper: print ".connect = <path>" for any attribute with connections
inline std::string fmt_connections(const std::vector<Path> &paths) {
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
inline void print_attr_metas_block(std::stringstream &ss,
                                   const AttrMeta &metas,
                                   const uint32_t indent) {
  if (metas.authored()) {
    ss << " (\n"
       << print_attr_metas(metas, indent + 1) << pprint::Indent(indent)
       << ")";
  }
}

// ============================================================================
// TimeSamples printing templates
// ============================================================================

template <typename T>
inline std::string print_typed_timesamples(const TypedTimeSamples<T> &v,
                                           const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

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

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

template <typename T>
inline std::string print_typed_token_timesamples(const TypedTimeSamples<T> &v,
                                                 const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

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

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

inline std::string print_str_timesamples(
    const TypedTimeSamples<std::string> &v, const uint32_t indent = 0) {
  std::stringstream ss;

  ss << "{\n";

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

  ss << pprint::Indent(indent) << "}\n";

  return ss.str();
}

// ============================================================================
// Animatable printing templates
// ============================================================================

template <typename T>
inline std::string print_animatable_default(const Animatable<T> &v,
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
inline std::string print_animatable_timesamples(const Animatable<T> &v,
                                                const uint32_t indent = 0) {
  std::stringstream ss;

  if (v.has_timesamples()) {
    ss << print_typed_timesamples(v.get_timesamples(), indent);
  }

  return ss.str();
}

// ============================================================================
// Typed attribute printing — TypedAttribute<Animatable<T>>
// ============================================================================

template <typename T>
inline std::string print_typed_attr(
    const TypedAttribute<Animatable<T>> &attr, const std::string &name,
    const uint32_t indent) {
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
    (void)is_timesamples;

    DCOUT("name " << name);
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_timesamples " << has_timesamples);
    DCOUT("has_default " << has_default);

    bool should_emit_declaration =
        attr.metas().authored() || attr.is_blocked() || has_default ||
        is_value_empty || ((!is_connection) && (!has_timesamples));
    if (should_emit_declaration) {

      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (has_default) {
        T a;
        if (pv.value().get_scalar(&a)) {
          std::stringstream val_ss;
          // Set prefix_columns for column-wrapping of arrays
          uint32_t pcols = static_cast<uint32_t>(
              pprint::Indent(indent).size() +
              std::string(value::TypeTraits<T>::type_name()).size() +
              1 + name.size() + 3);  // " " + name + " = "
          pprint::ScopedPrefixColumns spc(pcols);
          val_ss << a;
          std::string val_str = val_ss.str();
          if (!val_str.empty()) {
            ss << " = " << val_str;
          }
        }
      } else {
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

// ============================================================================
// Typed attribute printing — string specialization (Animatable<string>)
// ============================================================================

inline std::string print_str_attr(
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

    if (attr.metas().authored() || attr.is_blocked() || has_default ||
        is_value_empty || ((!is_connection) && (!is_timesamples))) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<std::string>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (pv.has_value()) {

        std::string a;
        if (pv.value().get_scalar(&a)) {
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

// ============================================================================
// Typed attribute printing — TypedAttribute<T> (uniform, non-Animatable)
// ============================================================================

template <typename T>
inline std::string print_typed_attr(const TypedAttribute<T> &attr,
                                    const std::string &name,
                                    const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    if (attr.metas().authored() || attr.is_blocked() || attr.has_value() ||
        attr.is_value_empty() || (!attr.is_connection())) {
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
          // Set prefix_columns for column-wrapping of arrays
          uint32_t pcols = static_cast<uint32_t>(
              pprint::Indent(indent).size() + 8 +  // "uniform "
              std::string(value::TypeTraits<T>::type_name()).size() +
              1 + name.size() + 3);  // " " + name + " = "
          pprint::ScopedPrefixColumns spc(pcols);
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

// ============================================================================
// Typed attribute printing — TypedAttributeWithFallback<Animatable<T>>
// ============================================================================

template <typename T>
inline std::string print_typed_attr(
    const TypedAttributeWithFallback<Animatable<T>> &attr,
    const std::string &name, const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    const auto &v = attr.get_value();

    bool is_connection = attr.is_connection();
    bool is_timesamples = v.is_timesamples();
    bool has_timesamples = v.has_timesamples();
    bool has_default = v.has_value();
    bool is_value_empty = attr.is_value_empty();
    (void)is_timesamples;

    DCOUT("name " << name);
    DCOUT("is_value_empty " << attr.is_value_empty());
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_timesamples " << has_timesamples);
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("has_default " << has_default);

    bool has_connections = attr.has_connections();
    bool is_connection_only = is_value_empty && has_connections;
    bool is_pure_empty_decl =
        is_value_empty && !has_connections && !has_timesamples;

    if (attr.metas().authored() || (!is_value_empty && has_default) ||
        is_pure_empty_decl ||
        ((!is_connection) && (!has_timesamples) && !is_connection_only)) {
      if (is_value_empty) {
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
      } else if (has_default) {
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
        {
          uint32_t pcols = static_cast<uint32_t>(
              pprint::Indent(indent).size() +
              std::string(value::TypeTraits<T>::type_name()).size() +
              1 + name.size() + 3);
          pprint::ScopedPrefixColumns spc(pcols);
          ss << " = " << print_animatable_default(v, indent);
        }
      } else {
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

// ============================================================================
// Typed terminal attribute printing
// ============================================================================

template <typename T>
inline std::string print_typed_terminal_attr(
    const TypedTerminalAttribute<T> &attr, const std::string &name,
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
         << print_attr_metas(attr.metas(), indent + 1)
         << pprint::Indent(indent) << ")";
    }
    ss << "\n";
  }

  return ss.str();
}

// ============================================================================
// Typed attribute printing — TypedAttributeWithFallback<T> (uniform)
// ============================================================================

template <typename T>
inline std::string print_typed_attr(
    const TypedAttributeWithFallback<T> &attr, const std::string &name,
    const uint32_t indent) {
  std::stringstream ss;

  if (attr.authored()) {

    // default
    if (attr.metas().authored() || attr.is_blocked() ||
        attr.is_value_empty() || (!attr.is_connection())) {
      ss << pprint::Indent(indent);
      ss << "uniform ";
      ss << value::TypeTraits<T>::type_name() << " " << name;

      if (attr.is_blocked()) {
        ss << " = None";
      } else if (attr.is_value_empty()) {
        // Definition only - no value to output
      } else {
        uint32_t pcols = static_cast<uint32_t>(
            pprint::Indent(indent).size() + 8 +  // "uniform "
            std::string(value::TypeTraits<T>::type_name()).size() +
            1 + name.size() + 3);
        pprint::ScopedPrefixColumns spc(pcols);
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

// ============================================================================
// Token attribute printing — TypedAttributeWithFallback<Animatable<T>> (token)
// ============================================================================

template <typename T>
inline std::string print_typed_token_attr(
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

// ============================================================================
// Token attribute printing — TypedAttributeWithFallback<T> (uniform token)
// ============================================================================

template <typename T>
inline std::string print_typed_token_attr(
    const TypedAttributeWithFallback<T> &attr, const std::string &name,
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
         << print_attr_metas(attr.metas(), indent + 1)
         << pprint::Indent(indent) << ")";
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

// ============================================================================
// Relationship printing helpers
// ============================================================================

inline std::string print_rel_only(const Relationship &rel,
                                  const std::string &name, uint32_t indent) {
  std::stringstream ss;

  ss << "rel " << name;

  if (!rel.has_value()) {
    // nothing todo
  } else if (rel.is_path()) {
    ss << " = " << rel.targetPath;
  } else if (rel.is_pathvector()) {
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

inline std::string print_relationship(const Relationship &rel,
                                      const ListEditQual &qual,
                                      const bool custom,
                                      const std::string &name,
                                      uint32_t indent) {
  std::stringstream ss;

  ss << pprint::Indent(indent);

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

// ============================================================================
// GPrim predefined properties template
// ============================================================================

template <typename T>
inline std::string print_gprim_predefined(const T &gprim,
                                          const uint32_t indent) {
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

// ============================================================================
// Prim header + axis printing templates
// ============================================================================

template <typename PrimT>
inline std::string print_prim_header(const PrimT &prim, const char *type_name,
                                     uint32_t indent) {
  std::stringstream ss;
  ss << pprint::Indent(indent) << to_string(prim.spec) << " " << type_name
     << " \"" << prim.name << "\"\n";
  if (prim.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(prim.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";
  return ss.str();
}

template <typename GeomT>
inline std::string print_axis_attr(const GeomT &geom, uint32_t indent) {
  if (!geom.axis.authored()) return {};
  std::stringstream ss;
  ss << pprint::Indent(indent) << "uniform token axis = \""
     << to_string(geom.axis.get_value()) << "\"\n";
  return ss.str();
}

}  // namespace tinyusdz
