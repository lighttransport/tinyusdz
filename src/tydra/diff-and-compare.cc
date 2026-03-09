#include "diff-and-compare.hh"
#include "../layer.hh"
#include "../common-macros.inc"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace tinyusdz {
namespace tydra {

namespace detail {

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
                           std::unordered_map<std::string, PropDiff> &propDiffs) {
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
      // Basic comparison - could be enhanced for deeper value comparison
      // For now, we assume properties with same name might be modified
      // This is a placeholder for more sophisticated value comparison
    }
  }
  
  if (!diff.addedProps.empty() || !diff.deletedProps.empty() || !diff.modifiedProps.empty()) {
    propDiffs[path] = diff;
  }
}

static bool ComputeDiffImpl(
  uint32_t depth,
  const std::string &path, const PrimSpec &lhs, const PrimSpec &rhs,
  std::unordered_map<std::string, PrimSpecDiff> &psDiffs,
  std::unordered_map<std::string, PropDiff> &propDiffs) {

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
  std::map<std::string, const PrimSpec*> lhs_child_map;
  std::map<std::string, const PrimSpec*> rhs_child_map;
  
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
      std::string child_path = path + "/" + child.name();
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
  std::unordered_map<std::string, PrimSpecDiff> &psDiffs,
  std::unordered_map<std::string, PropDiff> &propDiffs) {

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
  std::unordered_map<std::string, PrimSpecDiff> psDiffs;
  std::unordered_map<std::string, PropDiff> propDiffs;
  
  Diff(lhs, rhs, psDiffs, propDiffs);
  
  std::stringstream ss;
  ss << "--- " << lhs_name << std::endl;
  ss << "+++ " << rhs_name << std::endl;
  
  // Sort paths for consistent output
  std::vector<std::string> sortedPaths;
  for (const auto &entry : psDiffs) {
    sortedPaths.push_back(entry.first);
  }
  for (const auto &entry : propDiffs) {
    if (std::find(sortedPaths.begin(), sortedPaths.end(), entry.first) == sortedPaths.end()) {
      sortedPaths.push_back(entry.first);
    }
  }
  std::sort(sortedPaths.begin(), sortedPaths.end());
  
  for (const std::string &path : sortedPaths) {
    // PrimSpec changes
    auto psIt = psDiffs.find(path);
    if (psIt != psDiffs.end()) {
      const PrimSpecDiff &psDiff = psIt->second;
      
      if (!psDiff.deletedPS.empty()) {
        for (const std::string &name : psDiff.deletedPS) {
          ss << "- " << path << "/" << name << " (PrimSpec deleted)" << std::endl;
        }
      }
      
      if (!psDiff.addedPS.empty()) {
        for (const std::string &name : psDiff.addedPS) {
          ss << "+ " << path << "/" << name << " (PrimSpec added)" << std::endl;
        }
      }
      
      if (!psDiff.modifiedPS.empty()) {
        for (const std::string &name : psDiff.modifiedPS) {
          ss << "~ " << path << "/" << name << " (PrimSpec modified)" << std::endl;
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
        for (const std::string &name : propDiff.modifiedProps) {
          ss << "~ " << path << "." << name << " (Property modified)" << std::endl;
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
  std::unordered_map<std::string, PrimSpecDiff> psDiffs;
  std::unordered_map<std::string, PropDiff> propDiffs;
  
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
    ss << "]\n";
    
    ss << "    }";
  }
  ss << "\n  }\n";
  
  ss << "}\n";
  
  return ss.str();
}

} // namespace tydra
} // namespace tinyusdz
