// SPDX-License-Identifier: Apache 2.0
// Copyright 2019 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertinament Inc.

#include <algorithm>
#include <atomic>
// #include <cassert>
#include <cctype>  // std::tolower
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include "asset-resolution.hh"

//
#ifndef __wasi__
#include <thread>
#endif
//
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "io-util.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "prim-pprint-parallel.hh"
#include "str-util.hh"
#include "tiny-container.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usda-reader.hh"
#include "value-pprint.hh"
//
#include "common-macros.inc"

namespace tinyusdz {

#if 1
// For PUSH_ERROR_AND_RETURN
#define PushError(s) _err += s;
// #define PushWarn(s) if (warn) { (*warn) += s; }
#endif

namespace {

// Optimized version: iterative traversal using path components
// Avoids string concatenation and recursion by directly navigating to target
nonstd::optional<const Prim *> GetPrimAtPathIterative(
    const std::vector<Prim> &root_nodes,
    const Path &path) {

  const std::string &target_path = path.full_path_name();

  // Must be absolute path starting with '/'
  if (target_path.empty() || target_path[0] != '/') {
    return nonstd::nullopt;
  }

  // For paths like "/" only (root path), we don't have a prim
  if (target_path.size() == 1) {
    return nonstd::nullopt;
  }

  // Iteratively parse and traverse path components
  // No string allocations - uses compare() with indices
  const Prim *current = nullptr;
  const std::vector<Prim> *current_children = &root_nodes;

  size_t start = 1;  // skip leading '/'
  const size_t len = target_path.size();

  while (start < len) {
    // Find end of current component
    size_t end = start;
    while (end < len && target_path[end] != '/') {
      ++end;
    }

    if (end == start) {
      // Empty component (double slash), skip
      start = end + 1;
      continue;
    }

    // Search for matching child using direct string comparison
    // No string allocation - compare against substring
    const Prim *found = nullptr;
    const size_t component_len = end - start;

    for (const auto &child : *current_children) {
      const std::string &name = child.element_name();
      if (name.size() == component_len &&
          target_path.compare(start, component_len, name) == 0) {
        found = &child;
        break;
      }
    }

    if (!found) {
      return nonstd::nullopt;
    }

    current = found;
    current_children = &current->children();
    start = end + 1;
  }

  return current;
}

}  // namespace

//
// -- Stage
//

nonstd::expected<const Prim *, std::string> Stage::GetPrimAtPath(
    const Path &path) const {
  DCOUT("GetPrimAtPath : " << path.prim_part() << "(input path: " << path
                           << ")");

  if (!path.is_valid()) {
    DCOUT("Invalid path.");
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.is_relative_path()) {
    DCOUT("Relative path is todo.");
    // TODO:
    return nonstd::make_unexpected("Relative path is TODO.\n");
  }

  if (!path.is_absolute_path()) {
    DCOUT("Not absolute path.");
    return nonstd::make_unexpected(
        "Path is not absolute. Non-absolute Path is TODO.\n");
  }

  if (_dirty) {
    DCOUT("clear cache.");
    // Clear cache.
    _prim_path_cache.clear();

    _dirty = false;
  } else {
    // First find from a cache.
    auto ret = _prim_path_cache.find(path.prim_part());
    if (ret != _prim_path_cache.end()) {
      DCOUT("Found cache.");
      return ret->second;
    }
  }


  // Direct path-based lookup (no brute-force search)
  if (auto pv = GetPrimAtPathIterative(_root_nodes, path)) {
    // Add to cache.
    // Assume pointer address does not change unless dirty state.
    _prim_path_cache[path.prim_part()] = pv.value();
    return pv.value();
  }

  DCOUT("Not found.");
  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() +
                                 "> in the Stage.\n");
}

bool Stage::find_prim_at_path(const Path &path, const Prim *&prim,
                              std::string *err) const {
  nonstd::expected<const Prim *, std::string> ret = GetPrimAtPath(path);
  if (ret) {
    prim = ret.value();
    return true;
  } else {
    if (err) {
      (*err) = ret.error();
    }
    return false;
  }
}

bool Stage::find_prim_at_path(const Path &path, int64_t *prim_id,
                              std::string *err) const {
  if (!prim_id) {
    if (err) {
      (*err) = "`prim_id` argument is nullptr.\n";
    }
    return false;
  }

  nonstd::expected<const Prim *, std::string> ret = GetPrimAtPath(path);
  if (ret) {
    (*prim_id) = ret.value()->prim_id();
    return true;
  } else {
    if (err) {
      (*err) = ret.error();
    }
    return false;
  }
}

// Optimized iterative version using explicit stack
// Avoids recursion to save stack memory for deep hierarchies
static bool FindPrimByPrimIdIterative(uint64_t prim_id,
                               const std::vector<Prim> &root_nodes,
                               const Prim **primFound) {
  if (!primFound) {
    return false;
  }

  // Use explicit stack for DFS traversal (StackVector for stack allocation)
  // Store pointer to prim and current child index
  StackVector<std::pair<const Prim *, size_t>, 4> stack;
  stack.reserve(64);  // Pre-allocate for typical depth

  // Initialize stack with root nodes
  for (const auto &root : root_nodes) {
    if (root.prim_id() == int64_t(prim_id)) {
      (*primFound) = &root;
      return true;
    }
    if (!root.children().empty()) {
      stack.emplace_back(&root, 0);
    }
  }

  // Iterative DFS
  while (!stack.empty()) {
    auto &top = stack.back();
    const Prim *current = top.first;
    size_t &child_idx = top.second;

    if (child_idx >= current->children().size()) {
      // All children processed, backtrack
      stack.pop_back();
      continue;
    }

    const Prim &child = current->children()[child_idx];
    ++child_idx;  // Move to next child for when we return

    // Check if this is the target
    if (child.prim_id() == int64_t(prim_id)) {
      (*primFound) = &child;
      return true;
    }

    // Push child to stack if it has children
    if (!child.children().empty()) {
      stack.emplace_back(&child, 0);
    }
  }

  return false;
}

bool Stage::find_prim_by_prim_id(const uint64_t prim_id, const Prim *&prim,
                                 std::string *err) const {
  if (prim_id < 1) {
    if (err) {
      (*err) = "Input prim_id must be 1 or greater.";
    }
    return false;
  }

  if (_prim_id_dirty) {
    DCOUT("clear prim_id cache.");
    // Clear cache.
    _prim_id_cache.clear();

    _prim_id_dirty = false;
  } else {
    // First find from a cache.
    auto ret = _prim_id_cache.find(prim_id);
    if (ret != _prim_id_cache.end()) {
      DCOUT("Found cache.");
      prim = ret->second;
      return true;
    }
  }

  const Prim *p{nullptr};
  if (FindPrimByPrimIdIterative(prim_id, _root_nodes, &p)) {
    _prim_id_cache[prim_id] = p;
    prim = p;
    return true;
  }

  return false;
}

bool Stage::find_prim_by_prim_id(const uint64_t prim_id, Prim *&prim,
                                 std::string *err) {
  const Prim *c_prim{nullptr};
  if (!find_prim_by_prim_id(prim_id, c_prim, err)) {
    return false;
  }

  // remove const
  prim = const_cast<Prim *>(c_prim);

  return true;
}

nonstd::expected<const Prim *, std::string> Stage::GetPrimFromRelativePath(
    const Prim &root, const Path &path) const {
  // TODO: Resolve "../"
  // TODO: cache path

  if (!path.is_valid()) {
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.is_absolute_path()) {
    return nonstd::make_unexpected(
        "Path is absolute. Path must be relative.\n");
  }

  if (path.is_relative_path()) {
    // ok
  } else {
    return nonstd::make_unexpected("Invalid Path.\n");
  }

#if 0  // TODO
  Path abs_path = root.element_path();
  abs_path.AppendElement(path.GetPrimPart());

  DCOUT("root path = " << root.path());
  DCOUT("abs path = " << abs_path);

  // Brute-force search from Stage root.
  if (auto pv = GetPrimAtPathRec(&root, /* root */"", abs_path, /* depth */0)) {
    return pv.value();
  }

  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() +
                                 "> under Prim: " + to_string(root.path) +
                                 "\n");
#else
  (void)root;
  return nonstd::make_unexpected("GetPrimFromRelativePath is TODO");
#endif
}

bool Stage::find_prim_from_relative_path(const Prim &root,
                                         const Path &relative_path,
                                         const Prim *&prim,
                                         std::string *err) const {
  nonstd::expected<const Prim *, std::string> ret =
      GetPrimFromRelativePath(root, relative_path);
  if (ret) {
    prim = ret.value();
    return true;
  } else {
    if (err) {
      (*err) = ret.error();
    }
    return false;
  }
}

namespace {

#if 0 // Deprecated. TODO: remove
void PrimPrintRec(std::stringstream &ss, const Prim &prim, uint32_t indent) {
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
  //
  if (prim.variantSets().size()) {
    if (require_newline) {
      ss << "\n";
    }

    // need to add blank line after VariantSet stmt and before child Prims,
    // so set require_newline true
    require_newline = true;

    for (const auto &variantSet : prim.variantSets()) {
      ss << pprint::Indent(indent + 1) << "variantSet "
         << quote(variantSet.first) << " = {\n";

      for (const auto &variantItem : variantSet.second.variantSet) {
        ss << pprint::Indent(indent + 2) << quote(variantItem.first);

        const Variant &variant = variantItem.second;

        if (variant.metas().authored()) {
          ss << " (\n";
          ss << print_prim_metas(variant.metas(), indent + 3);
          ss << pprint::Indent(indent + 2) << ")";
        }

        ss << " {\n";

        ss << print_props(variant.properties(), indent + 3);

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
              PrimPrintRec(ss, *(it->second), indent + 3);
              if (i != (variant.primChildren().size() - 1)) {
                ss << "\n";
              }
            } else {
              // TODO: Report warning?
            }
          }

        } else {
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            PrimPrintRec(ss, variant.primChildren()[i], indent + 3);
            if (i != (variant.primChildren().size() - 1)) {
              ss << "\n";
            }
          }
        }

        ss << pprint::Indent(indent + 2) << "}\n";
      }

      ss << pprint::Indent(indent + 1) << "}\n";
    }
  }

  DCOUT(prim.element_name() << " num_children = " << prim.children().size());

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
          PrimPrintRec(ss, *(it->second), indent + 1);
        } else {
          // TODO: Report warning?
        }
      }

    } else {
      for (size_t i = 0; i < prim.children().size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        PrimPrintRec(ss, prim.children()[i], indent + 1);
      }
    }
  }

  ss << pprint::Indent(indent) << "}\n";
}
#endif

}  // namespace

std::string Stage::ExportToString(bool relative_path, bool parallel) const {
  (void)relative_path; // TODO
#if !defined(TINYUSDZ_ENABLE_THREAD)
  (void)parallel; // Threading disabled
#endif

  std::stringstream ss;

  ss << "#usda 1.0\n";

  std::stringstream meta_ss;
  meta_ss << print_layer_metas(stage_metas, /* indent */1);
  if (meta_ss.str().size()) {
    ss << "(\n";
    ss << meta_ss.str();
    ss << ")\n";
  }

  ss << "\n";

  if (stage_metas.primChildren.size() == _root_nodes.size()) {
    std::map<std::string, const Prim *> primNameTable;
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      primNameTable.emplace(_root_nodes[i].element_name(), &_root_nodes[i]);
    }

#if defined(TINYUSDZ_ENABLE_THREAD)
    if (parallel) {
      // Parallel printing path
      std::vector<const Prim*> ordered_prims;
      ordered_prims.reserve(stage_metas.primChildren.size());

      for (size_t i = 0; i < stage_metas.primChildren.size(); i++) {
        value::token nameTok = stage_metas.primChildren[i];
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          ordered_prims.push_back(it->second);
        }
      }

      prim::ParallelPrintConfig config;
      ss << prim::print_prims_parallel(ordered_prims, 0, config);
    } else
#endif  // TINYUSDZ_ENABLE_THREAD
    {
      // Sequential printing path (original)
      for (size_t i = 0; i < stage_metas.primChildren.size(); i++) {
        value::token nameTok = stage_metas.primChildren[i];
        DCOUT(fmt::format("primChildren  {}/{} = {}", i,
                          stage_metas.primChildren.size(), nameTok.str()));
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          //PrimPrintRec(ss, *(it->second), 0);
          ss << prim::print_prim(*(it->second), 0);
          if (i != (stage_metas.primChildren.size() - 1)) {
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
      std::vector<const Prim*> prims;
      prims.reserve(_root_nodes.size());
      for (size_t i = 0; i < _root_nodes.size(); i++) {
        prims.push_back(&_root_nodes[i]);
      }

      prim::ParallelPrintConfig config;
      ss << prim::print_prims_parallel(prims, 0, config);
    } else
#endif  // TINYUSDZ_ENABLE_THREAD
    {
      // Sequential printing path (original)
      for (size_t i = 0; i < _root_nodes.size(); i++) {
        //PrimPrintRec(ss, _root_nodes[i], 0);
        ss << prim::print_prim(_root_nodes[i], 0);

        if (i != (_root_nodes.size() - 1)) {
          ss << "\n";
        }
      }
    }
  }

  return ss.str();
}

bool Stage::allocate_prim_id(uint64_t *prim_id) const {
  if (!prim_id) {
    return false;
  }

  uint64_t val;
  if (_prim_id_allocator.Allocate(&val)) {
    (*prim_id) = val;
    return true;
  }

  return false;
}

bool Stage::release_prim_id(const uint64_t prim_id) const {
  return _prim_id_allocator.Release(prim_id);
}

bool Stage::has_prim_id(const uint64_t prim_id) const {
  return _prim_id_allocator.Has(prim_id);
}

namespace {

// Iterative version of ComputeAbsPathAndAssignPrimIdRec
// Uses explicit stack to avoid recursion
bool ComputeAbsPathAndAssignPrimIdIterative(const Stage &stage,
                                            std::vector<Prim> &root_prims,
                                            bool assign_prim_id,
                                            bool force_assign_prim_id,
                                            std::string *err) {
  // Stack entry: (prim pointer, parent path, child index)
  struct StackEntry {
    Prim *prim;
    Path parent_path;
    size_t child_idx;

    StackEntry(Prim *p, Path pp) : prim(p), parent_path(std::move(pp)), child_idx(0) {}
  };

  StackVector<StackEntry, 4> stack;
  stack.reserve(64);

  Path root_path("/", "");

  // Process each root prim
  for (Prim &root : root_prims) {
    if (root.element_name().empty()) {
      if (err) {
        (*err) += "Prim's elementName is empty. Prim's parent Path = /\n";
      }
      return false;
    }

    // Compute path and assign ID for root
    Path abs_path = root_path.AppendPrim(root.element_name());
    root.absolute_path() = abs_path;

    if (assign_prim_id) {
      if (force_assign_prim_id || (root.prim_id() < 1)) {
        uint64_t prim_id{0};
        if (!stage.allocate_prim_id(&prim_id)) {
          if (err) {
            (*err) += "Failed to assign unique Prim ID.\n";
          }
          return false;
        }
        root.prim_id() = int64_t(prim_id);
      }
    }

    if (!root.children().empty()) {
      stack.emplace_back(&root, abs_path);
    }

    // Process tree iteratively
    while (!stack.empty()) {
      auto &top = stack.back();
      Prim *current = top.prim;
      size_t &child_idx = top.child_idx;

      if (child_idx >= current->children().size()) {
        stack.pop_back();
        continue;
      }

      Prim &child = current->children()[child_idx];
      ++child_idx;

      if (child.element_name().empty()) {
        if (err) {
          (*err) += "Prim's elementName is empty. Prim's parent Path = " +
                    top.parent_path.full_path_name() + "\n";
        }
        return false;
      }

      Path child_abs_path = top.parent_path.AppendPrim(child.element_name());
      child.absolute_path() = child_abs_path;

      if (assign_prim_id) {
        if (force_assign_prim_id || (child.prim_id() < 1)) {
          uint64_t prim_id{0};
          if (!stage.allocate_prim_id(&prim_id)) {
            if (err) {
              (*err) += "Failed to assign unique Prim ID.\n";
            }
            return false;
          }
          child.prim_id() = int64_t(prim_id);
        }
      }

      if (!child.children().empty()) {
        stack.emplace_back(&child, child_abs_path);
      }
    }
  }

  return true;
}

}  // namespace

bool Stage::compute_absolute_prim_path_and_assign_prim_id(
    bool force_assign_prim_id) {
  if (!ComputeAbsPathAndAssignPrimIdIterative(*this, _root_nodes,
                                              /* assign_prim_id */ true,
                                              force_assign_prim_id, &_err)) {
    return false;
  }

  // TODO: Only set dirty when prim_id changed.
  _prim_id_dirty = true;

  return true;
}

bool Stage::compute_absolute_prim_path() {
  if (!ComputeAbsPathAndAssignPrimIdIterative(*this, _root_nodes,
                                              /* assign_prim_id */ false,
                                              /* force_assign_prim_id */ true,
                                              &_err)) {
    return false;
  }

  return true;
}

bool Stage::add_root_prim(Prim &&prim, bool rename_prim_name) {
  std::string elementName = prim.element_name();

  if (elementName.empty()) {
    if (rename_prim_name) {

      // assign default name `default`
      elementName = "default";

      if (!SetPrimElementName(prim.get_data(), elementName)) {
        PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName");
      }
      prim.element_path() = Path(elementName, /* prop_part */"");
    } else {
      PUSH_ERROR_AND_RETURN("Prim has empty elementName.");
    }
  }

  if (_root_nodes.size() != _root_node_nameSet.size()) {
    // Rebuild nameSet
    _root_node_nameSet.clear();
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      if (_root_nodes[i].element_name().empty()) {
        PUSH_ERROR_AND_RETURN("Internal error: Existing root Prim's elementName is empty.");
      }

      if (_root_node_nameSet.count(_root_nodes[i].element_name())) {
        PUSH_ERROR_AND_RETURN("Internal error: Stage contains root Prim with same elementName.");
      }

      _root_node_nameSet.insert(_root_nodes[i].element_name());
    }
  }

  if (_root_node_nameSet.count(elementName)) {
    if (rename_prim_name) {
      std::string unique_name;
      if (!makeUniqueName(_root_node_nameSet, elementName, &unique_name)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Internal error. cannot assign unique name for `{}`.\n", elementName));
      }

      elementName = unique_name;

      // Need to modify both Prim::data::name and Prim::elementPath
      if (!SetPrimElementName(prim.get_data(), elementName)) {
        PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName.");
      }
      prim.element_path() = Path(elementName, /* prop_part */"");
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("Prim name(elementName) {} already exists in children.\n", prim.element_name()));
    }
  }


  _root_node_nameSet.insert(elementName);
  _root_nodes.emplace_back(std::move(prim));

  _dirty = true;

  return true;


}

bool Stage::replace_root_prim(const std::string &prim_name, Prim &&prim) {

  if (prim_name.empty()) {
    PUSH_ERROR_AND_RETURN(fmt::format("prim_name is empty."));
  }

  if (!ValidatePrimElementName(prim_name)) {
    PUSH_ERROR_AND_RETURN(fmt::format("`{}` is not a valid Prim name.", prim_name));
  }

  if (_root_nodes.size() != _root_node_nameSet.size()) {
    // Rebuild nameSet
    _root_node_nameSet.clear();
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      if (_root_nodes[i].element_name().empty()) {
        PUSH_ERROR_AND_RETURN("Internal error: Existing root Prim's elementName is empty.");
      }

      if (_root_node_nameSet.count(_root_nodes[i].element_name())) {
        PUSH_ERROR_AND_RETURN("Internal error: Stage contains root Prim with same elementName.");
      }

      _root_node_nameSet.insert(_root_nodes[i].element_name());
    }
  }

  // Simple linear scan
  auto result = std::find_if(_root_nodes.begin(), _root_nodes.end(), [prim_name](const Prim &p) {
    return (p.element_name() == prim_name);
  });

  if (result != _root_nodes.end()) {

    // Need to modify both Prim::data::name and Prim::elementPath
    if (!SetPrimElementName(prim.get_data(), prim_name)) {
      PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName.");
    }
    prim.element_path() = Path(prim_name, /* prop_part */"");

    (*result) = std::move(prim); // replace

  } else {

    // Need to modify both Prim::data::name and Prim::elementPath
    if (!SetPrimElementName(prim.get_data(), prim_name)) {
      PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName.");
    }
    prim.element_path() = Path(prim_name, /* prop_part */"");

    _root_node_nameSet.insert(prim_name);
    _root_nodes.emplace_back(std::move(prim)); // add
  }

  _dirty = true;

  return true;
}

namespace {

// Iterative version of DumpPrimTree using explicit stack
// Avoids recursion to save stack memory for deep hierarchies
std::string DumpPrimTreeIterative(const std::vector<Prim> &root_prims) {
  std::stringstream ss;

  // Stack entries: (prim pointer, depth, child index)
  // child_idx == SIZE_MAX means we haven't processed this node yet
  struct StackEntry {
    const Prim *prim;
    uint32_t depth;
    size_t child_idx;
    StackEntry(const Prim *p, uint32_t d) : prim(p), depth(d), child_idx(SIZE_MAX) {}
  };

  StackVector<StackEntry, 4> stack;
  stack.reserve(64);

  // Push root prims in reverse order to process in forward order
  for (auto it = root_prims.rbegin(); it != root_prims.rend(); ++it) {
    stack.emplace_back(&(*it), 0);
  }

  constexpr uint32_t kMaxDepth = 1024 * 1024 * 128;

  while (!stack.empty()) {
    StackEntry &entry = stack.back();

    if (entry.depth > kMaxDepth) {
      stack.pop_back();
      continue;
    }

    if (entry.child_idx == SIZE_MAX) {
      // First visit: output this node
      ss << pprint::Indent(entry.depth) << "\"" << entry.prim->element_name() << "\" "
         << entry.prim->absolute_path() << "\n";
      ss << pprint::Indent(entry.depth + 1) << fmt::format("prim_id {}", entry.prim->prim_id())
         << "\n";
      entry.child_idx = 0;
    }

    // Process children
    const auto &children = entry.prim->children();
    if (entry.child_idx < children.size()) {
      // Push next child and increment index
      size_t idx = entry.child_idx++;
      stack.emplace_back(&children[idx], entry.depth + 1);
    } else {
      // All children processed, pop this node
      stack.pop_back();
    }
  }

  return ss.str();
}

}  // namespace

std::string Stage::dump_prim_tree() const {
  return DumpPrimTreeIterative(_root_nodes);
}

size_t Stage::estimate_memory_usage() const {
  size_t total = sizeof(Stage);

  // Stage metadata (uses LayerMetas::estimate_memory_usage)
  total += stage_metas.estimate_memory_usage() - sizeof(StageMetas);

  // Estimate memory for root prims (recursive via Prim::estimate_memory_usage)
  total += _root_nodes.capacity() * sizeof(Prim);
  for (const auto& prim : _root_nodes) {
    total += prim.estimate_memory_usage() - sizeof(Prim);  // Avoid double counting sizeof
  }

  // Internal string storage
  total += _warn.capacity();
  total += _err.capacity();

  // Prim ID management
  total += _prim_id_cache.size() * (sizeof(uint64_t) + sizeof(const Prim*)) * 2; // Rough estimate for map overhead
  // Note: _prim_id_allocator internal memory is harder to estimate without its implementation details

  return total;
}

}  // namespace tinyusdz
