/*
Copyright (c) 2019 - Present, Syoyo Fujita.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Syoyo Fujita nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <algorithm>
#include <atomic>
//#include <cassert>
#include <cctype>  // std::tolower
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

//
#ifndef __wasi__
#include <thread>
#endif
//
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pprinter.hh"
#include "str-util.hh"
#include "tinyusdz.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "usda-reader.hh"
#include "io-util.hh"
//
#include "common-macros.inc"

namespace tinyusdz {

#if 1
// For PUSH_ERROR_AND_RETURN
#define PushError(s) \
  _err += s;     
//#define PushWarn(s) if (warn) { (*warn) += s; }
#endif

namespace {

#if 0  // not used yet

///
/// Node represents scene graph node.
/// This does not contain leaf node inormation.
///
class Node {
 public:
  // -2 = initialize as invalid node
  Node() : _parent(-2) {}

  Node(int64_t parent, Path &path) : _parent(parent), _path(path) {}

  int64_t GetParent() const { return _parent; }

  const std::vector<size_t> &GetChildren() const { return _children; }

  ///
  /// child_name is used when reconstructing scene graph.
  ///
  bool AddChildren(const std::string &child_name, size_t node_index) {
    if (_primChildren.count(child_name)) {
      return false;
    }
    //assert(_primChildren.count(child_name) == 0);
    _primChildren.emplace(child_name);
    _children.push_back(node_index);

    return true;
  }

  ///
  /// Get full path(e.g. `/muda/dora/bora` when the parent is `/muda/dora` and
  /// this node is `bora`)
  ///
  // std::string GetFullPath() const { return _path.full_path_name(); }

  ///
  /// Get local path
  ///
  std::string GetLocalPath() const { return _path.full_path_name(); }

  const Path &GetPath() const { return _path; }

  // NodeType GetNodeType() const { return _node_type; }

  const std::unordered_set<std::string> &GetPrimChildren() const {
    return _primChildren;
  }

  void SetAssetInfo(const value::dict &dict) { _assetInfo = dict; }

  const value::dict &GetAssetInfo() const { return _assetInfo; }

 private:
  int64_t
      _parent;  // -1 = this node is the root node. -2 = invalid or leaf node
  std::vector<size_t> _children;                  // index to child nodes.
  std::unordered_set<std::string> _primChildren;  // List of name of child nodes

  Path _path;  // local path
  value::dict _assetInfo;

  // NodeType _node_type;
};
#endif

nonstd::optional<const Prim *> GetPrimAtPathRec(const Prim *parent,
                                                const std::string &parent_path,
                                                const Path &path,
                                                const uint32_t depth) {
  std::string abs_path;
  // if (auto pv = GetPrimElementName(parent->data())) {
  {
    std::string elementName = parent->element_path().prim_part();
    DCOUT(pprint::Indent(depth) << "Prim elementName = " << elementName);
    DCOUT(pprint::Indent(depth) << "Given Path = " << path);
    // fully absolute path
    abs_path = parent_path + "/" + elementName;
    DCOUT(pprint::Indent(depth) << "abs_path = " << abs_path);
    DCOUT(pprint::Indent(depth)
          << "queriying path = " << path.full_path_name());
    if (abs_path == path.full_path_name()) {
      DCOUT(pprint::Indent(depth)
            << "Got it! Found Prim at Path = " << abs_path);
      return parent;
    }
  }

  DCOUT(pprint::Indent(depth)
        << "# of children : " << parent->children().size());
  for (const auto &child : parent->children()) {
    // const std::string &p = parent->elementPath.full_path_name();
    DCOUT(pprint::Indent(depth + 1) << "Parent path : " << abs_path);
    if (auto pv = GetPrimAtPathRec(&child, abs_path, path, depth + 1)) {
      return pv.value();
    }
  }

  return nonstd::nullopt;
}

} // namespace local

//
// -- Stage
//

nonstd::expected<const Prim *, std::string> Stage::GetPrimAtPath(
    const Path &path) const {
  DCOUT("GerPrimAtPath : " << path.prim_part() << "(input path: " << path
                           << ")");

  if (_dirty) {
    // Clear cache.
    _prim_path_cache.clear();

    _dirty = false;
  } else {
    // First find from a cache.
    auto ret = _prim_path_cache.find(path.prim_part());
    if (ret != _prim_path_cache.end()) {
      return ret->second;
    }
  }

  if (!path.is_valid()) {
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.is_relative_path()) {
    // TODO:
    return nonstd::make_unexpected("Relative path is TODO.\n");
  }

  if (!path.is_absolute_path()) {
    return nonstd::make_unexpected(
        "Path is not absolute. Non-absolute Path is TODO.\n");
  }

  // Brute-force search.
  for (const auto &parent : root_nodes) {
    if (auto pv =
            GetPrimAtPathRec(&parent, /* root */ "", path, /* depth */ 0)) {
      // Add to cache.
      // Assume pointer address does not change unless dirty state.
      _prim_path_cache[path.prim_part()] = pv.value();
      return pv.value();
    }
  }

  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() +
                                 "> int the Stage.\n");
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

bool Stage::LoadLayerFromMemory(const uint8_t *addr, const size_t nbytes, const std::string &asset_name, Layer *layer) {

  tinyusdz::StreamReader sr(addr, nbytes, /* swap endian */ false);
  tinyusdz::usda::USDAReader reader(&sr);

  // TODO: Uase AssetResolver
  //reader.SetBaseDir(base_dir);

  if (!reader.Read(ascii::LoadState::REFERENCE)) {
    return false;
  }

  if (!reader.GetAsLayer(layer)) {
    PUSH_ERROR_AND_RETURN("Failed to retrieve USD data as Layer: filepath = " << asset_name);
  }

  return false;
}

bool Stage::LoadLayerFromFile(const std::string &_filename, Layer *layer) {
  // TODO: Setup AssetResolver.

  std::string filepath = io::ExpandFilePath(_filename, /* userdata */ nullptr);
  std::string base_dir = io::GetBaseDir(_filename);

  DCOUT("load layer from file: " << filepath);

  std::string err;
  std::vector<uint8_t> data;
  size_t max_bytes = std::numeric_limits<size_t>::max(); // TODO:
  if (!io::ReadWholeFile(&data, &err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    PUSH_ERROR_AND_RETURN("Read file failed: " + err);
  }

  return LoadLayerFromMemory(data.data(), data.size(), filepath, layer);
}

bool Stage::LoadSubLayers(std::vector<Layer> *sublayers) {
  (void)sublayers;
  return false;
}

namespace {

void PrimPrintRec(std::stringstream &ss, const Prim &prim, uint32_t indent) {
  ss << "\n";
  // Currently, Prim's elementName is read from name variable in concrete Prim class(e.g. Xform::name).
  // TODO: use prim.elementPath for elementName.
  ss << pprint_value(prim.data(), indent, /* closing_brace */ false);

  DCOUT("num_children = " << prim.children().size());
  for (const auto &child : prim.children()) {
    PrimPrintRec(ss, child, indent + 1);
  }

  ss << pprint::Indent(indent) << "}\n";
}

}  // namespace

std::string Stage::ExportToString() const {
  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  if (stage_metas.doc.value.empty()) {
    ss << pprint::Indent(1) << "doc = \"Exporterd from TinyUSDZ v" << tinyusdz::version_major
       << "." << tinyusdz::version_minor << "." << tinyusdz::version_micro
       << tinyusdz::version_rev << "\"\n";
  } else {
    ss << pprint::Indent(1) << "doc = " << to_string(stage_metas.doc) << "\n";
  }

  if (stage_metas.metersPerUnit.authored()) {
    ss << pprint::Indent(1) << "metersPerUnit = " << stage_metas.metersPerUnit.get_value() << "\n";
  }

  if (stage_metas.upAxis.authored()) {
    ss << pprint::Indent(1) << "upAxis = " << quote(to_string(stage_metas.upAxis.get_value()))
       << "\n";
  }

  if (stage_metas.timeCodesPerSecond.authored()) {
    ss << pprint::Indent(1) << "timeCodesPerSecond = "
       << stage_metas.timeCodesPerSecond.get_value() << "\n";
  }

  if (stage_metas.startTimeCode.authored()) {
    ss << pprint::Indent(1) << "startTimeCode = " << stage_metas.startTimeCode.get_value() << "\n";
  }

  if (stage_metas.endTimeCode.authored()) {
    ss << pprint::Indent(1) << "endTimeCode = " << stage_metas.endTimeCode.get_value() << "\n";
  }

  if (stage_metas.framesPerSecond.authored()) {
    ss << pprint::Indent(1) << "framesPerSecond = " << stage_metas.framesPerSecond.get_value() << "\n";
  }

  // TODO: Do not print subLayers when consumed(after composition evaluated)
  if (stage_metas.subLayers.size()) {
    ss << pprint::Indent(1) << "subLayers = " << stage_metas.subLayers << "\n";
  }

  if (stage_metas.defaultPrim.str().size()) {
    ss << pprint::Indent(1) << "defaultPrim = " << tinyusdz::quote(stage_metas.defaultPrim.str())
       << "\n";
  }

  if (stage_metas.autoPlay.authored()) {
    ss << pprint::Indent(1) << "autoPlay = " << to_string(stage_metas.autoPlay.get_value()) << "\n";
  }

  if (stage_metas.playbackMode.authored()) {
    auto v = stage_metas.playbackMode.get_value();
    if (v == StageMetas::PlaybackMode::PlaybackModeLoop) {
      ss << pprint::Indent(1) << "playbackMode = \"loop\"\n";
    } else { // None
      ss << pprint::Indent(1) << "playbackMode = \"none\"\n";
    }
  }

  if (!stage_metas.comment.value.empty()) {
    // Stage meta omits 'comment'
    ss << pprint::Indent(1) << to_string(stage_metas.comment) << "\n";
  }

  if (stage_metas.customLayerData.size()) {
    ss << print_customData(stage_metas.customLayerData, "customLayerData",
                           /* indent */ 1);
  }

  // TODO: write other header data.
  ss << ")\n";
  ss << "\n";

  for (const auto &item : root_nodes) {
    PrimPrintRec(ss, item, 0);
  }

  return ss.str();
}

}  // namespace tinyusdz
