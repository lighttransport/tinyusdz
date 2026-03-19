// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Enum / Path string converters (extracted from pprinter.hh).
//
#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "prim-types.hh"

namespace tinyusdz {

namespace pprint {

// Declared here, defined in pprint-meta.cc (or pprinter.cc).
void SetIndentString(const std::string &s);
std::string Indent(uint32_t level);

}  // namespace pprint

// --- simple enum/struct to_string converters (prim-types.hh only) ---------

std::string to_string(Visibility v);
std::string to_string(Orientation o);
std::string to_string(Extent e);
std::string to_string(Interpolation interp);
std::string to_string(Axis axis);
std::string to_string(ListEditQual qual);
std::string to_string(Specifier specifier);
std::string to_string(Purpose purpose);
std::string to_string(Permission permission);
std::string to_string(Variability variability);
std::string to_string(SpecType spec_type);
std::string to_string(Kind kind);

std::string to_string(const Reference &reference);
std::string to_string(const Payload &payload);

std::string to_string(const XformOp::OpType &ty);

std::string to_string(const Path &path, bool show_full_path = true);
std::string to_string(const std::vector<Path> &v, bool show_full_path = true);

// For debugging.
std::string dump_path(const Path &p);

std::string to_string(const APISchemas::APIName &name);
std::string to_string(const CustomDataType &customData);

std::string to_string(const CollectionInstance::ExpansionRule rule);

// --- generic template helpers (inline) ------------------------------------

template <typename T>
std::string to_string(const std::vector<T> &v, const uint32_t level = 0) {
  std::stringstream ss;
  ss << pprint::Indent(level) << "[";
  for (size_t i = 0; i < v.size(); i++) {
    ss << to_string(v[i]);
    if (i != (v.size() - 1)) {
      ss << ", ";
    }
  }
  ss << "]";
  return ss.str();
}

template <typename T>
std::string to_string(const ListOp<T> &op, const uint32_t indent_level = 0) {
  std::stringstream ss;
  ss << pprint::Indent(indent_level) << "ListOp(isExplicit " << op.IsExplicit()
     << ") {\n";
  ss << pprint::Indent(indent_level)
     << "  explicit_items = " << to_string(op.GetExplicitItems()) << "\n";
  ss << pprint::Indent(indent_level)
     << "  added_items = " << to_string(op.GetAddedItems()) << "\n";
  ss << pprint::Indent(indent_level)
     << "  appended_items = " << to_string(op.GetAppendedItems()) << "\n";
  ss << pprint::Indent(indent_level)
     << "  prepended_items = " << to_string(op.GetPrependedItems()) << "\n";
  ss << pprint::Indent(indent_level)
     << "  deleted_items = " << to_string(op.GetDeletedItems()) << "\n";
  ss << pprint::Indent(indent_level)
     << "  ordered_items = " << to_string(op.GetOrderedItems()) << "\n";
  ss << pprint::Indent(indent_level) << "}";
  return ss.str();
}

}  // namespace tinyusdz
