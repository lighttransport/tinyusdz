// Utility and attribute/meta pretty-printer implementations split from pprinter.cc
// SPDX-License-Identifier: Apache 2.0
#include "value-pprint.hh" // for dtos
#include "str-util.hh" // for quote
#include "pprinter-util.hh"
#include "pprinter.hh"
#include <sstream>

namespace tinyusdz {

std::string print_attr_metas(const AttrMeta &meta, const uint32_t indent) {
  std::stringstream ss;
  if (meta.interpolation) {
    ss << pprint::Indent(indent)
       << "interpolation = " << quote(to_string(meta.interpolation.value()))
       << "\n";
  }
  if (meta.elementSize) {
    ss << pprint::Indent(indent)
       << "elementSize = " << to_string(meta.elementSize.value()) << "\n";
  }
  if (meta.bindMaterialAs) {
    ss << pprint::Indent(indent)
       << "bindMaterialAs = " << quote(to_string(meta.bindMaterialAs.value()))
       << "\n";
  }
  if (meta.connectability) {
    ss << pprint::Indent(indent)
       << "connectability = " << quote(to_string(meta.connectability.value()))
       << "\n";
  }
  if (meta.displayName) {
    ss << pprint::Indent(indent)
       << "displayName = " << quote(meta.displayName.value()) << "\n";
  }
  if (meta.displayGroup) {
    ss << pprint::Indent(indent)
       << "displayGroup = " << quote(meta.displayGroup.value()) << "\n";
  }
  if (meta.outputName) {
    ss << pprint::Indent(indent)
       << "outputName = " << quote(to_string(meta.outputName.value())) << "\n";
  }
  if (meta.renderType) {
    ss << pprint::Indent(indent)
       << "renderType = " << quote(to_string(meta.renderType.value())) << "\n";
  }
  if (meta.sdrMetadata) {
    ss << pprint::Indent(indent)
       << print_customData(meta.sdrMetadata.value(), "sdrMetadata", indent);
  }
  if (meta.hidden) {
    ss << pprint::Indent(indent)
       << "hidden = " << to_string(meta.hidden.value()) << "\n";
  }
  if (meta.comment) {
    ss << pprint::Indent(indent)
       << "comment = " << to_string(meta.comment.value()) << "\n";
  }
  if (meta.weight) {
    ss << pprint::Indent(indent) << "weight = " << dtos(meta.weight.value())
       << "\n";
  }
  if (meta.customData) {
    ss << print_customData(meta.customData.value(), "customData", indent);
  }
  for (const auto &item : meta.meta) {
    ss << print_meta(item.second, indent, /* emit_type_name */false, item.first);
  }
  for (const auto &item : meta.stringData) {
    ss << pprint::Indent(indent) << to_string(item) << "\n";
  }
  return ss.str();
}

std::string print_relationship(const Relationship &rel, ListEditQual qual, bool custom, const std::string &name, uint32_t indent) {
  (void)custom;
  // TODO: Implement actual pretty-printing for relationships
  std::stringstream ss;
  ss << "rel " << name;
  if (!rel.has_value()) {
    // nothing to do
  } else if (rel.is_path()) {
    ss << " = " << rel.targetPath;
  } else if (rel.is_pathvector()) {
    ss << " = " << rel.targetPathVector;
  } else if (rel.is_blocked()) {
    ss << " = None";
  } else {
    ss << " = [InternalError]";
  }
  // Optionally print list edit qualifier
  if (qual != ListEditQual::ResetToExplicit) {
    ss << " (" << to_string(qual) << ")";
  }
  if (rel.metas().authored()) {
    ss << " (\n" << print_attr_metas(rel.metas(), indent + 1) << pprint::Indent(indent) << ")";
  }
  ss << "\n";
  return ss.str();
}

} // namespace tinyusdz
