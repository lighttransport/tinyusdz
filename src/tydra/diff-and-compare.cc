#include "diff-and-compare.hh"
#include "../layer.hh"
#include "../pprint-enum.hh"
#include "../value-pprint.hh"
#include "../common-macros.inc"
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace tinyusdz {
namespace tydra {

namespace detail {

static std::string TruncateForDiff(const std::string &s) {
  constexpr size_t kMaxDiffValueChars = 240;
  if (s.size() <= kMaxDiffValueChars) {
    return s;
  }
  return s.substr(0, kMaxDiffValueChars) + "...";
}

static std::string JoinPrimPath(const std::string &parent,
                                const std::string &child) {
  if (parent.empty() || parent == "/") {
    return "/" + child;
  }
  return parent + "/" + child;
}

static std::string JoinPathList(const std::vector<Path> &paths) {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < paths.size(); ++i) {
    if (i > 0) {
      ss << ", ";
    }
    ss << "<" << paths[i].full_path_name() << ">";
  }
  ss << "]";
  return ss.str();
}

static std::string NormalizeAssetPathForDiff(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  while (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
    path.erase(0, 2);
  }
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

static std::string AssetPathLeafForDiff(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

static bool HasPathComponentSuffix(const std::string &path,
                                   const std::string &suffix) {
  if (path == suffix) {
    return true;
  }
  if (path.size() <= suffix.size()) {
    return false;
  }
  const size_t offset = path.size() - suffix.size();
  return path.compare(offset, suffix.size(), suffix) == 0 &&
         path[offset - 1] == '/';
}

static bool AssetPathStringsEquivalentForDiff(const std::string &lhs_path,
                                              const std::string &rhs_path) {
  const std::string lhs = NormalizeAssetPathForDiff(lhs_path);
  const std::string rhs = NormalizeAssetPathForDiff(rhs_path);
  if (lhs == rhs) {
    return true;
  }
  if (HasPathComponentSuffix(lhs, rhs) || HasPathComponentSuffix(rhs, lhs)) {
    return true;
  }

  const std::string lhs_leaf = AssetPathLeafForDiff(lhs);
  const std::string rhs_leaf = AssetPathLeafForDiff(rhs);
  return !lhs_leaf.empty() && lhs_leaf == rhs_leaf;
}

template <typename T>
static bool NumericValuesEquivalentForDiff(const value::Value &lhs,
                                           const value::Value &rhs,
                                           double abs_tol,
                                           double rel_tol) {
  const auto lhs_num = lhs.get_value<T>(false);
  const auto rhs_num = rhs.get_value<T>(false);
  if (!lhs_num || !rhs_num) {
    return false;
  }

  const double lhs_value = static_cast<double>(lhs_num.value());
  const double rhs_value = static_cast<double>(rhs_num.value());
  const double diff = std::fabs(lhs_value - rhs_value);
  if (diff <= abs_tol) {
    return true;
  }

  const double scale = std::max(std::fabs(lhs_value), std::fabs(rhs_value));
  return diff <= rel_tol * scale;
}

static bool ValuesEquivalentForDiff(const value::Value &lhs,
                                    const value::Value &rhs) {
  const auto lhs_asset = lhs.get_value<value::AssetPath>(false);
  const auto rhs_asset = rhs.get_value<value::AssetPath>(false);
  if (lhs_asset && rhs_asset) {
    return AssetPathStringsEquivalentForDiff(lhs_asset.value().GetAssetPath(),
                                             rhs_asset.value().GetAssetPath());
  }
  if (NumericValuesEquivalentForDiff<float>(lhs, rhs, 1.0e-6, 1.0e-6)) {
    return true;
  }
  if (NumericValuesEquivalentForDiff<double>(lhs, rhs, 1.0e-12, 1.0e-12)) {
    return true;
  }
  return value::pprint_value(lhs) == value::pprint_value(rhs);
}

static std::string ListEditQualForDiff(ListEditQual qual) {
  std::string s = tinyusdz::to_string(qual);
  if (s.empty()) {
    s = "explicit";
  }
  return s;
}

static const char *RelationshipTypeName(Relationship::Type type) {
  switch (type) {
    case Relationship::Type::DefineOnly:
      return "define";
    case Relationship::Type::Path:
      return "path";
    case Relationship::Type::PathVector:
      return "pathVector";
    case Relationship::Type::ValueBlock:
      return "blocked";
  }
  return "unknown";
}

static std::string FormatAttributeForDiff(const Attribute &attr) {
  std::stringstream ss;
  ss << "attr";
  ss << " type=" << attr.type_name();
  ss << " variability=" << tinyusdz::to_string(attr.variability());
  if (attr.is_varying_authored()) {
    ss << " varyingAuthored=true";
  }

  if (attr.has_connections()) {
    ss << " connections=" << JoinPathList(attr.connections());
  }
  if (attr.is_blocked()) {
    ss << " value=None";
  } else if (attr.has_value()) {
    ss << " value="
       << TruncateForDiff(value::pprint_value(attr.get_var().value_raw()));
  }
  if (attr.has_timesamples()) {
    const auto &samples = attr.get_var().ts_raw().get_samples();
    ss << " timeSamples=" << samples.size();
    if (!samples.empty()) {
      ss << " firstTime=" << samples.front().t
         << " lastTime=" << samples.back().t;
    }
  }

  return ss.str();
}

static std::string FormatRelationshipForDiff(const Relationship &rel) {
  std::stringstream ss;
  ss << "rel";
  ss << " type=" << RelationshipTypeName(rel.type);
  if (rel.is_varying_authored()) {
    ss << " varyingAuthored=true";
  }

  if (rel.is_path()) {
    ss << " target=<" << rel.targetPath.full_path_name() << ">";
  } else if (rel.is_pathvector()) {
    ss << " targets=" << JoinPathList(rel.targetPathVector);
  } else if (rel.is_blocked()) {
    ss << " target=None";
  }

  return ss.str();
}

static std::string FormatPropertyForDiff(const Property &prop) {
  std::stringstream ss;
  if (prop.has_custom()) {
    ss << "custom ";
  }
  ss << "listOp=" << ListEditQualForDiff(prop.get_listedit_qual()) << " ";

  if (prop.is_attribute()) {
    ss << FormatAttributeForDiff(prop.get_attribute());
  } else if (prop.is_relationship()) {
    ss << FormatRelationshipForDiff(prop.get_relationship());
  } else {
    ss << "empty";
  }

  return ss.str();
}

struct FNV1StringHash {
  size_t operator()(const std::string &s) const noexcept {
    static constexpr uint64_t kFNV_Prime = 0x00000100000001B3ull;
    static constexpr uint64_t kFNV_Offset_Basis = 0xcbf29ce484222325ull;

    uint64_t hash = kFNV_Offset_Basis;
    for (char ch : s) {
      hash = (kFNV_Prime * hash) ^ static_cast<unsigned char>(ch);
    }
    return static_cast<size_t>(hash);
  }
};

static bool CompareAttributeValues(const Attribute &lhs, const Attribute &rhs) {
  if (lhs.type_name() != rhs.type_name()) {
    const uint32_t lhsUnderlying = value::GetUnderlyingTypeId(lhs.type_name());
    const uint32_t rhsUnderlying = value::GetUnderlyingTypeId(rhs.type_name());
    if (lhsUnderlying == value::TYPE_ID_INVALID ||
        rhsUnderlying == value::TYPE_ID_INVALID ||
        lhsUnderlying != rhsUnderlying) {
      return false;
    }
  }
  if (lhs.variability() != rhs.variability()) return false;
  if (lhs.is_varying_authored() != rhs.is_varying_authored()) return false;
  if (lhs.has_connections() != rhs.has_connections()) return false;
  if (lhs.has_value() != rhs.has_value()) return false;
  if (lhs.has_timesamples() != rhs.has_timesamples()) return false;
  if (lhs.is_blocked() != rhs.is_blocked()) return false;

  if (lhs.has_connections() && (lhs.connections() != rhs.connections())) return false;

  const auto &lhsVar = lhs.get_var();
  const auto &rhsVar = rhs.get_var();
  if (lhsVar.type_id() != rhsVar.type_id()) {
    const uint32_t lhsUnderlying = value::GetUnderlyingTypeId(lhsVar.type_name());
    const uint32_t rhsUnderlying = value::GetUnderlyingTypeId(rhsVar.type_name());
    if (lhsUnderlying == value::TYPE_ID_INVALID ||
        rhsUnderlying == value::TYPE_ID_INVALID ||
        lhsUnderlying != rhsUnderlying) {
      return false;
    }
  }

  if (lhs.has_value()) {
    if (!ValuesEquivalentForDiff(lhsVar.value_raw(), rhsVar.value_raw())) {
      return false;
    }
  }

  if (lhs.has_timesamples()) {
    const auto &lhsSamples = lhsVar.ts_raw().get_samples();
    const auto &rhsSamples = rhsVar.ts_raw().get_samples();
    if (lhsSamples.size() != rhsSamples.size()) return false;

    const double eps = std::numeric_limits<double>::epsilon();
    for (size_t i = 0; i < lhsSamples.size(); ++i) {
      if (std::fabs(lhsSamples[i].t - rhsSamples[i].t) >= eps) return false;
      if (lhsSamples[i].blocked != rhsSamples[i].blocked) return false;
      if (!lhsSamples[i].blocked &&
          !ValuesEquivalentForDiff(lhsSamples[i].value, rhsSamples[i].value)) {
        return false;
      }
    }
  }

  return true;
}

static bool CompareRelationshipValues(const Relationship &lhs, const Relationship &rhs) {
  if (lhs.type != rhs.type) return false;
  if (lhs.get_listedit_qual() != rhs.get_listedit_qual()) return false;
  if (lhs.is_varying_authored() != rhs.is_varying_authored()) return false;
  if (!(lhs.targetPath == rhs.targetPath)) return false;
  if (lhs.targetPathVector != rhs.targetPathVector) return false;
  return true;
}

static bool ArePropertiesEquivalent(const Property &lhs, const Property &rhs) {
  if (lhs.has_custom() != rhs.has_custom()) return false;
  if (lhs.get_listedit_qual() != rhs.get_listedit_qual()) return false;
  if (lhs.get_property_type() != rhs.get_property_type()) return false;

  if (lhs.is_attribute() != rhs.is_attribute()) return false;
  if (lhs.is_relationship() != rhs.is_relationship()) return false;

  if (lhs.is_attribute()) {
    return CompareAttributeValues(lhs.get_attribute(), rhs.get_attribute());
  }

  if (lhs.is_relationship()) {
    return CompareRelationshipValues(lhs.get_relationship(), rhs.get_relationship());
  }

  return lhs.is_empty() == rhs.is_empty();
}

static bool ComparePrimSpecs(const PrimSpec &lhs, const PrimSpec &rhs) {
  // Compare basic properties
  if (lhs.name() != rhs.name()) return false;
  if (lhs.specifier() != rhs.specifier()) return false;
  if (lhs.typeName() != rhs.typeName()) return false;
  
  // Compare properties
  const auto &lhs_props = lhs.props();
  const auto &rhs_props = rhs.props();
  if (lhs_props.size() != rhs_props.size()) return false;
  
  for (const auto &prop : lhs_props) {
    auto it = rhs_props.find(prop.first);
    if (it == rhs_props.end()) return false;
    // Basic property comparison - could be enhanced for deeper comparison
  }
  
  // Compare children count
  if (lhs.children().size() != rhs.children().size()) return false;
  
  return true;
}

static void ComputePropDiff(const std::string &path, const PrimSpec &lhs, const PrimSpec &rhs, 
                           tinyusdz::HashMap<std::string, PropDiff> &propDiffs) {
  const auto &lhs_props = lhs.props();
  const auto &rhs_props = rhs.props();
  
  PropDiff diff;
  
  // Find added properties (in rhs but not in lhs)
  for (const auto &prop : rhs_props) {
    if (lhs_props.find(prop.first) == lhs_props.end()) {
      diff.addedProps.push_back(prop.first);
    }
  }
  
  // Find deleted properties (in lhs but not in rhs)
  for (const auto &prop : lhs_props) {
    if (rhs_props.find(prop.first) == rhs_props.end()) {
      diff.deletedProps.push_back(prop.first);
    }
  }
  
  // Find modified properties (different values)
  for (const auto &prop : lhs_props) {
    auto it = rhs_props.find(prop.first);
    if (it != rhs_props.end()) {
      if (!ArePropertiesEquivalent(prop.second, it->second)) {
        diff.modifiedProps.push_back(prop.first);
        diff.modifiedPropDetails.push_back(
            {prop.first, FormatPropertyForDiff(prop.second),
             FormatPropertyForDiff(it->second)});
      }
    }
  }
  
  if (!diff.addedProps.empty() || !diff.deletedProps.empty() || !diff.modifiedProps.empty()) {
    propDiffs[path] = diff;
  }
}

static bool ComputeDiffImpl(
  uint32_t depth,
  const std::string &path, const PrimSpec &lhs, const PrimSpec &rhs,
  tinyusdz::HashMap<std::string, PrimSpecDiff> &psDiffs,
  tinyusdz::HashMap<std::string, PropDiff> &propDiffs) {

  if (size_t(depth) > kMaxDefaultTraversalLimit) {
    return false;
  }

  bool hasDiff = false;
  
  // Compare this PrimSpec
  if (!ComparePrimSpecs(lhs, rhs)) {
    hasDiff = true;
  }
  
  // Compute property differences
  ComputePropDiff(path, lhs, rhs, propDiffs);
  
  // Compare children
  const auto &lhs_children = lhs.children();
  const auto &rhs_children = rhs.children();
  
  // Build maps for easier lookup
  tinyusdz::HashMap<std::string, const PrimSpec *, FNV1StringHash> lhs_child_map;
  tinyusdz::HashMap<std::string, const PrimSpec *, FNV1StringHash> rhs_child_map;
  lhs_child_map.reserve(lhs_children.size());
  rhs_child_map.reserve(rhs_children.size());
  
  for (const auto &child : lhs_children) {
    lhs_child_map[child.name()] = &child;
  }
  
  for (const auto &child : rhs_children) {
    rhs_child_map[child.name()] = &child;
  }
  
  PrimSpecDiff psDiff;
  
  // Find added children (in rhs but not in lhs)
  for (const auto &child : rhs_children) {
    if (lhs_child_map.find(child.name()) == lhs_child_map.end()) {
      psDiff.addedPS.push_back(child.name());
      hasDiff = true;
    }
  }
  
  // Find deleted children (in lhs but not in rhs)
  for (const auto &child : lhs_children) {
    if (rhs_child_map.find(child.name()) == rhs_child_map.end()) {
      psDiff.deletedPS.push_back(child.name());
      hasDiff = true;
    }
  }
  
  // Recursively compare common children
  for (const auto &child : lhs_children) {
    auto it = rhs_child_map.find(child.name());
    if (it != rhs_child_map.end()) {
      std::string child_path = detail::JoinPrimPath(path, child.name());
      if (ComputeDiffImpl(depth + 1, child_path, child, *it->second, psDiffs, propDiffs)) {
        psDiff.modifiedPS.push_back(child.name());
        hasDiff = true;
      }
    }
  }
  
  if (!psDiff.addedPS.empty() || !psDiff.deletedPS.empty() || !psDiff.modifiedPS.empty()) {
    psDiffs[path] = psDiff;
  }

  return hasDiff;
}

static std::string EscapeJSON(const std::string &str) {
  std::string result;
  for (char c : str) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
    }
  }
  return result;
}

} // namespace detail

void Diff(const Layer &lhs, const Layer &rhs,
  tinyusdz::HashMap<std::string, PrimSpecDiff> &psDiffs,
  tinyusdz::HashMap<std::string, PropDiff> &propDiffs) {

  const auto &lhs_primspecs = lhs.primspecs();
  const auto &rhs_primspecs = rhs.primspecs();
  
  PrimSpecDiff rootDiff;
  
  // Find added root primspecs (in rhs but not in lhs)
  for (const auto &rhs_prim : rhs_primspecs) {
    if (lhs_primspecs.find(rhs_prim.first) == lhs_primspecs.end()) {
      rootDiff.addedPS.push_back(rhs_prim.first);
    }
  }
  
  // Find deleted root primspecs (in lhs but not in rhs)
  for (const auto &lhs_prim : lhs_primspecs) {
    if (rhs_primspecs.find(lhs_prim.first) == rhs_primspecs.end()) {
      rootDiff.deletedPS.push_back(lhs_prim.first);
    }
  }
  
  // Compare common root primspecs
  for (const auto &lhs_prim : lhs_primspecs) {
    auto it = rhs_primspecs.find(lhs_prim.first);
    if (it != rhs_primspecs.end()) {
      std::string prim_path = "/" + lhs_prim.first;
      if (detail::ComputeDiffImpl(0, prim_path, lhs_prim.second, it->second, psDiffs, propDiffs)) {
        rootDiff.modifiedPS.push_back(lhs_prim.first);
      }
    }
  }
  
  // Add root level differences if any
  if (!rootDiff.addedPS.empty() || !rootDiff.deletedPS.empty() || !rootDiff.modifiedPS.empty()) {
    psDiffs["/"] = rootDiff;
  }
}

std::string DiffToText(const Layer &lhs, const Layer &rhs,
                       const std::string &lhs_name,
                       const std::string &rhs_name) {
  tinyusdz::HashMap<std::string, PrimSpecDiff> psDiffs;
  tinyusdz::HashMap<std::string, PropDiff> propDiffs;
  
  Diff(lhs, rhs, psDiffs, propDiffs);
  
  std::stringstream ss;
  ss << "--- " << lhs_name << std::endl;
  ss << "+++ " << rhs_name << std::endl;
  
  // Sort paths for consistent output
  std::unordered_set<std::string, detail::FNV1StringHash> uniquePaths;
  uniquePaths.reserve(psDiffs.size() + propDiffs.size());
  for (const auto &entry : psDiffs) {
    uniquePaths.insert(entry.first);
  }
  for (const auto &entry : propDiffs) {
    uniquePaths.insert(entry.first);
  }
  std::vector<std::string> sortedPaths;
  sortedPaths.reserve(uniquePaths.size());
  sortedPaths.insert(sortedPaths.end(), uniquePaths.begin(), uniquePaths.end());
  std::sort(sortedPaths.begin(), sortedPaths.end());
  
  for (const std::string &path : sortedPaths) {
    // PrimSpec changes
    auto psIt = psDiffs.find(path);
    if (psIt != psDiffs.end()) {
      const PrimSpecDiff &psDiff = psIt->second;
      
      if (!psDiff.deletedPS.empty()) {
        for (const std::string &name : psDiff.deletedPS) {
          ss << "- " << detail::JoinPrimPath(path, name) << " (PrimSpec deleted)" << std::endl;
        }
      }
      
      if (!psDiff.addedPS.empty()) {
        for (const std::string &name : psDiff.addedPS) {
          ss << "+ " << detail::JoinPrimPath(path, name) << " (PrimSpec added)" << std::endl;
        }
      }
      
      if (!psDiff.modifiedPS.empty()) {
        for (const std::string &name : psDiff.modifiedPS) {
          ss << "~ " << detail::JoinPrimPath(path, name) << " (PrimSpec modified)" << std::endl;
        }
      }
    }
    
    // Property changes
    auto propIt = propDiffs.find(path);
    if (propIt != propDiffs.end()) {
      const PropDiff &propDiff = propIt->second;
      
      if (!propDiff.deletedProps.empty()) {
        for (const std::string &name : propDiff.deletedProps) {
          ss << "- " << path << "." << name << " (Property deleted)" << std::endl;
        }
      }
      
      if (!propDiff.addedProps.empty()) {
        for (const std::string &name : propDiff.addedProps) {
          ss << "+ " << path << "." << name << " (Property added)" << std::endl;
        }
      }
      
      if (!propDiff.modifiedProps.empty()) {
        for (const PropDiff::ModifiedProp &modified :
             propDiff.modifiedPropDetails) {
          ss << "~ " << path << "." << modified.name
             << " (Property modified)" << std::endl;
          ss << "  - " << modified.lhs << std::endl;
          ss << "  + " << modified.rhs << std::endl;
        }
      }
    }
  }
  
  if (ss.str().empty() || ss.str() == "--- " + lhs_name + "\n+++ " + rhs_name + "\n") {
    return "No differences found.\n";
  }
  
  return ss.str();
}

std::string DiffToJSON(const Layer &lhs, const Layer &rhs,
                       const std::string &lhs_name,
                       const std::string &rhs_name) {
  tinyusdz::HashMap<std::string, PrimSpecDiff> psDiffs;
  tinyusdz::HashMap<std::string, PropDiff> propDiffs;
  
  Diff(lhs, rhs, psDiffs, propDiffs);
  
  std::stringstream ss;
  ss << "{\n";
  ss << "  \"comparison\": {\n";
  ss << "    \"left\": \"" << detail::EscapeJSON(lhs_name) << "\",\n";
  ss << "    \"right\": \"" << detail::EscapeJSON(rhs_name) << "\"\n";
  ss << "  },\n";
  
  // PrimSpec differences
  ss << "  \"primspec_diffs\": {\n";
  bool firstPrimDiff = true;
  for (const auto &entry : psDiffs) {
    if (!firstPrimDiff) ss << ",\n";
    firstPrimDiff = false;
    
    const std::string &path = entry.first;
    const PrimSpecDiff &diff = entry.second;
    
    ss << "    \"" << detail::EscapeJSON(path) << "\": {\n";
    
    // Added PrimSpecs
    ss << "      \"added\": [";
    for (size_t i = 0; i < diff.addedPS.size(); ++i) {
      if (i > 0) ss << ", ";
      ss << "\"" << detail::EscapeJSON(diff.addedPS[i]) << "\"";
    }
    ss << "],\n";
    
    // Deleted PrimSpecs
    ss << "      \"deleted\": [";
    for (size_t i = 0; i < diff.deletedPS.size(); ++i) {
      if (i > 0) ss << ", ";
      ss << "\"" << detail::EscapeJSON(diff.deletedPS[i]) << "\"";
    }
    ss << "],\n";
    
    // Modified PrimSpecs
    ss << "      \"modified\": [";
    for (size_t i = 0; i < diff.modifiedPS.size(); ++i) {
      if (i > 0) ss << ", ";
      ss << "\"" << detail::EscapeJSON(diff.modifiedPS[i]) << "\"";
    }
    ss << "]\n";
    
    ss << "    }";
  }
  ss << "\n  },\n";
  
  // Property differences
  ss << "  \"property_diffs\": {\n";
  bool firstPropDiff = true;
  for (const auto &entry : propDiffs) {
    if (!firstPropDiff) ss << ",\n";
    firstPropDiff = false;
    
    const std::string &path = entry.first;
    const PropDiff &diff = entry.second;
    
    ss << "    \"" << detail::EscapeJSON(path) << "\": {\n";
    
    // Added Properties
    ss << "      \"added\": [";
    for (size_t i = 0; i < diff.addedProps.size(); ++i) {
      if (i > 0) ss << ", ";
      ss << "\"" << detail::EscapeJSON(diff.addedProps[i]) << "\"";
    }
    ss << "],\n";
    
    // Deleted Properties
    ss << "      \"deleted\": [";
    for (size_t i = 0; i < diff.deletedProps.size(); ++i) {
      if (i > 0) ss << ", ";
      ss << "\"" << detail::EscapeJSON(diff.deletedProps[i]) << "\"";
    }
    ss << "],\n";
    
    // Modified Properties
    ss << "      \"modified\": [";
    for (size_t i = 0; i < diff.modifiedProps.size(); ++i) {
      if (i > 0) ss << ", ";
      ss << "\"" << detail::EscapeJSON(diff.modifiedProps[i]) << "\"";
    }
    ss << "],\n";

    ss << "      \"modified_details\": [";
    for (size_t i = 0; i < diff.modifiedPropDetails.size(); ++i) {
      if (i > 0) ss << ", ";
      const PropDiff::ModifiedProp &modified = diff.modifiedPropDetails[i];
      ss << "{\"name\":\"" << detail::EscapeJSON(modified.name)
         << "\", \"left\":\"" << detail::EscapeJSON(modified.lhs)
         << "\", \"right\":\"" << detail::EscapeJSON(modified.rhs)
         << "\"}";
    }
    ss << "]\n";
    
    ss << "    }";
  }
  ss << "\n  }\n";
  
  ss << "}\n";
  
  return ss.str();
}

} // namespace tydra
} // namespace tinyusdz
