// SPDX-License-Identifier: Apache 2.0
// Shared helper declarations for pretty printer utilities
#pragma once

#include "pprinter.hh"
#include "value-pprint.hh" // for quote, dtos
#include <string>
#include <vector>

namespace tinyusdz {

// Attribute/relationship pretty-printer helpers

template <typename T>
std::string print_typed_attr(const TypedAttribute<Animatable<T>> &attr, const std::string &name, const uint32_t indent);

template <typename T>
std::string print_typed_attr(const TypedAttribute<T> &attr, const std::string &name, const uint32_t indent);

template <typename T>
std::string print_typed_attr(const TypedAttributeWithFallback<T> &attr, const std::string &name, const uint32_t indent);

template <typename T>
std::string print_typed_attr(const TypedAttributeWithFallback<Animatable<T>> &attr, const std::string &name, const uint32_t indent);

// Token attribute pretty-printer (declaration only)
template <typename T>
std::string print_typed_token_attr(const T &attr, const std::string &name, const uint32_t indent);

// Gprim predefined attribute printer (declaration only)
template <typename T>
std::string print_gprim_predefined(const T &gprim, const uint32_t indent);

// Relationship pretty-printer (declaration only)
template <typename T>
std::string print_relationship(const T &rel, const uint32_t indent);
// Overload for multi-argument usage in pprinter-geom.cc
std::string print_relationship(const Relationship &rel, ListEditQual qual, bool custom, const std::string &name, uint32_t indent);

std::string print_attr_metas(const AttrMeta &meta, const uint32_t indent);

// Add other helpers as needed (declarations only)
// e.g. print_rel_only, print_str_timesamples

template <typename T>
std::string print_animatable_default(const Animatable<T> &v, const uint32_t indent);

template <typename T>
std::string print_animatable_timesamples(const Animatable<T> &v, const uint32_t indent);

template <typename T>
std::string print_typed_timesamples(const TypedTimeSamples<T> &v, const uint32_t indent);

} // namespace tinyusdz

template <typename T>
std::string tinyusdz::print_animatable_default(const Animatable<T> &v, const uint32_t indent) {
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
std::string tinyusdz::print_animatable_timesamples(const Animatable<T> &v, const uint32_t indent) {
  std::stringstream ss;
  if (v.has_timesamples()) {
    ss << print_typed_timesamples(v.get_timesamples(), indent);
  }
  return ss.str();
}

template <typename T>
std::string tinyusdz::print_typed_attr(const TypedAttribute<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {
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
    DCOUT("name " << name);
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("has_timesamples " << has_timesamples);
    DCOUT("has_default " << has_default);
    if (attr.metas().authored() || attr.is_blocked() || has_default || is_value_empty || ((!is_connection) && (!is_timesamples))) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;
      if (attr.is_blocked()) {
        ss << " = None";
      } else if (has_default) {
        T a;
        if (pv.value().get_scalar(&a)) {
          ss << " = " << a;
        } else {
          ss << " = [InternalError]";
        }
      } else {
        // is_value_empty
      }
      if (attr.metas().authored()) {
        ss << "(\n"
           << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
           << ")";
      }
      ss << "\n";
    }
    if (has_timesamples) {
      ss << pprint::Indent(indent);
      ss << value::TypeTraits<T>::type_name() << " " << name;
      ss << ".timeSamples = "
         << print_typed_timesamples(pv.value().get_timesamples(), indent);
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
std::string tinyusdz::print_typed_attr(const TypedAttribute<T> &attr, const std::string &name, const uint32_t indent) {
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
      if (attr.metas().authored()) {
        ss << " (\n"
           << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
           << ")";
      }
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
std::string tinyusdz::print_typed_attr(const TypedAttributeWithFallback<T> &attr, const std::string &name, const uint32_t indent) {
  std::stringstream ss;
  if (attr.authored()) {
    // default
    if (attr.metas().authored() || attr.is_blocked() || (!attr.is_connection())) {
      ss << pprint::Indent(indent);
      ss << "uniform ";
      ss << value::TypeTraits<T>::type_name() << " " << name;
      if (attr.is_blocked()) {
        ss << " = None";
      } else {
        ss << " = " << attr.get_value();
      }
      if (attr.metas().authored()) {
        ss << " (\n"
           << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
           << ")";
      }
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
std::string tinyusdz::print_typed_attr(const TypedAttributeWithFallback<Animatable<T>> &attr, const std::string &name, const uint32_t indent) {
  std::stringstream ss;
  if (attr.authored()) {
    const auto &v = attr.get_value();
    bool is_connection = attr.is_connection();
    bool is_timesamples = v.is_timesamples();
    bool has_value = attr.has_value();
    bool is_value_empty = attr.is_value_empty();
    DCOUT("name " << name);
    DCOUT("is_value_empty " << attr.is_value_empty());
    DCOUT("is_connection " << is_connection);
    DCOUT("is_timesamples " << is_timesamples);
    DCOUT("is_value_empty " << is_value_empty);
    DCOUT("has_value " << has_value);
    if (attr.metas().authored() || has_value || is_value_empty || ((!is_connection) && (!is_timesamples))) {
      if (has_value) {
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
        ss << " = " << print_animatable_default(v, indent);
      } else {
        ss << pprint::Indent(indent);
        ss << value::TypeTraits<T>::type_name() << " " << name;
      }
      if (attr.metas().authored()) {
        ss << " (\n"
           << print_attr_metas(attr.metas(), indent + 1) << pprint::Indent(indent)
           << ")";
      }
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
std::string tinyusdz::print_typed_token_attr(const T &, const std::string &, const uint32_t) {
  return "[print_typed_token_attr not implemented]";
}

template <typename T>
std::string tinyusdz::print_gprim_predefined(const T &, const uint32_t) {
  return "[print_gprim_predefined not implemented]";
}

template <typename T>
std::string tinyusdz::print_relationship(const T &, const uint32_t) {
  return "[print_relationship not implemented]";
}

template <typename T>
std::string tinyusdz::print_typed_timesamples(const TypedTimeSamples<T> &v, const uint32_t indent) {
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
