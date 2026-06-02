// SPDX-License-Identifier: Apache-2.0
#include "gui_stringify.hh"

#include "value-pprint.hh"

namespace tusdview {

namespace {
std::string Sanitize(std::string s, size_t maxLen = 240) {
  // Flatten newlines so the value fits a single table cell, then truncate.
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }
  if (s.size() > maxLen) {
    s.resize(maxLen);
    s += " ...";
  }
  return s;
}
}  // namespace

std::string PropertyToString(const tinyusdz::Property& prop) {
  if (prop.is_relationship()) {
    std::vector<tinyusdz::Path> targets = prop.get_relationTargets();
    if (targets.empty()) return "(no targets)";
    std::string s;
    for (size_t i = 0; i < targets.size(); ++i) {
      if (i) s += ", ";
      s += targets[i].full_path_name();
    }
    return Sanitize(s);
  }

  if (prop.is_attribute()) {
    const tinyusdz::Attribute& a = prop.get_attribute();
    if (a.has_connections()) {
      std::string t = prop.value_type_name();
      return t.empty() ? "< connection >" : ("< connection: " + t + " >");
    }
    if (a.has_value()) {
      const tinyusdz::value::Value& v = a.get_var().value_raw();
      return Sanitize(tinyusdz::value::pprint_value(v, 0, /*closing_brace=*/false));
    }
    if (a.has_timesamples()) {
      return "(timeSamples)";
    }
    std::string t = prop.value_type_name();
    return t.empty() ? "(no value)" : ("(" + t + ", no value)");
  }

  return "(empty)";
}

std::string PrimMetaSummary(const tinyusdz::Prim& prim) {
  const auto& m = prim.metas();
  std::string s;
  auto add = [&](const char* k, const std::string& v) {
    if (!s.empty()) s += "\n";
    s += k;
    s += ": ";
    s += v;
  };
  if (m.has_kind()) add("kind", m.get_kind_str());
  if (m.has_active()) add("active", m.get_active() ? "true" : "false");
  if (m.has_hidden()) add("hidden", m.get_hidden() ? "true" : "false");
  if (m.has_displayName()) add("displayName", m.get_displayName());
  if (m.has_doc()) add("documentation", m.get_doc().value);
  if (m.has_comment()) add("comment", m.get_comment().value);
  if (m.has_customData()) add("customData", "(authored)");
  return s;
}

std::string AttrMetaSummary(const tinyusdz::Attribute& attr) {
  const auto& m = attr.metas();
  std::string s;
  auto add = [&](const std::string& kv) {
    if (!s.empty()) s += "  ";
    s += kv;
  };
  if (m.has_interpolation()) add("interpolation=" + m.get_interpolation().str());
  if (m.has_elementSize()) add("elementSize=" + std::to_string(m.get_elementSize()));
  if (m.has_displayName()) add("displayName=" + m.get_displayName());
  if (m.has_customData()) add("customData");
  return s;
}

}  // namespace tusdview
