// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#pragma once

#include <string>
#include <unordered_set>

#include "crate-format.hh"
#include "nonstd/optional.hpp"
#include "stream-reader.hh"

namespace tinyusdz {
namespace crate {

struct CrateReaderConfig {
  int numThreads = -1;

  // For malcious Crate data.
  // Set limits to prevent infinite-loop, buffer-overrun, etc.
  size_t maxDictElements = 4096;
  size_t maxArrayElements = 1024*1024*1024; // 1M
};

///
/// Crate(binary data) reader
///
class CrateReader {
 public:

  ///
  /// Intermediate Node data structure.
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
    void AddChildren(const std::string &child_name, size_t node_index) {
      assert(_primChildren.count(child_name) == 0);
      _primChildren.emplace(child_name);
      _children.push_back(node_index);
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

    // crate::CrateDataType GetNodeDataType() const { return _node_type; }

    const std::unordered_set<std::string> &GetPrimChildren() const {
      return _primChildren;
    }

    void SetAssetInfo(const value::dict &dict) { _assetInfo = dict; }

    const value::dict &GetAssetInfo() const { return _assetInfo; }

   private:
    int64_t
        _parent;  // -1 = this node is the root node. -2 = invalid or leaf node
    std::vector<size_t> _children;  // index to child nodes.
    std::unordered_set<std::string>
        _primChildren;  // List of name of child nodes

    Path _path;  // local path
    value::dict _assetInfo;

    // value::TypeId _node_type;
    // NodeType _node_type;
  };

 public:

 private:
  CrateReader() = delete;

 public:
  CrateReader(StreamReader *sr, const CrateReaderConfig &config = CrateReaderConfig());
  ~CrateReader();

  bool ReadBootStrap();
  bool ReadTOC();

  ///
  /// Read TOC section
  ///
  bool ReadSection(crate::Section *s);

  // Read known sections
  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();

  bool BuildLiveFieldSets();

  std::string GetError();
  std::string GetWarning();

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const;

  /// -------------------------------------
  /// Following Methods are valid after successfull parsing of Crate data.
  ///
  size_t NumNodes() const {
    return _nodes.size();
  }

  const std::vector<Node> GetNodes() const {
    return _nodes;
  }

  const std::vector<value::token> GetTokens() const {
    return _tokens;
  }

  const std::vector<crate::Index> GetStringIndices() const {
    return _string_indices;
  }

  const std::vector<crate::Field> &GetFields() const {
    return _fields;
  }

  const std::vector<crate::Index> &GetFieldsetIndices() const {
    return _fieldset_indices;
  }

  const std::vector<Path> &GetPaths() const {
    return _paths;
  }

  const std::vector<crate::Spec> &GetSpecs() const {
    return _specs;
  }

  
  const std::map<crate::Index, FieldValuePairVector> &GetLiveFieldSets() const {
    return _live_fieldsets;
  }

#if 0
  // FIXME: May not need this
  const std::vector<Path> &GetPaths() const {
    return _paths;
  }
#endif



  const nonstd::optional<value::token> GetToken(crate::Index token_index) const;
  const nonstd::optional<value::token> GetStringToken(
      crate::Index string_index) const;

  bool HasField(const std::string &key) const;
  nonstd::optional<crate::Field> GetField(crate::Index index) const;
  nonstd::optional<std::string> GetFieldString(crate::Index index) const;
  nonstd::optional<std::string> GetSpecString(crate::Index index) const;

  size_t NumPaths() const {
    return _paths.size();
  }

  nonstd::optional<Path> GetPath(crate::Index index) const;
  nonstd::optional<std::string> GetPathString(crate::Index index) const;



  ///
  /// Find if a field with (`name`, `tyname`) exists in FieldValuePairVector.
  ///
  bool HasFieldValuePair(const FieldValuePairVector &fvs,
                         const std::string &name, const std::string &tyname);

  ///
  /// Find if a field with `name`(type can be arbitrary) exists in
  /// FieldValuePairVector.
  ///
  bool HasFieldValuePair(const FieldValuePairVector &fvs,
                         const std::string &name);

  nonstd::expected<FieldValuePair, std::string> GetFieldValuePair(
      const FieldValuePairVector &fvs, const std::string &name,
      const std::string &tyname);

  nonstd::expected<FieldValuePair, std::string> GetFieldValuePair(
      const FieldValuePairVector &fvs, const std::string &name);

  bool ParseAttribute(const FieldValuePairVector &fvs,
                                    PrimAttrib *attr,
                                    const std::string &prop_name);

 private:


  bool BuildDecompressedPathsImpl(
      std::vector<uint32_t> const &pathIndexes,
      std::vector<int32_t> const &elementTokenIndexes,
      std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath);

  bool UnpackValueRep(const crate::ValueRep &rep, crate::CrateValue *value);
  bool UnpackInlinedValueRep(const crate::ValueRep &rep,
                             crate::CrateValue *value);

  //
  // Construct node hierarchy.
  //
  bool BuildNodeHierarchy(std::vector<uint32_t> const &pathIndexes,
                          std::vector<int32_t> const &elementTokenIndexes,
                          std::vector<int32_t> const &jumps, size_t curIndex,
                          int64_t parentNodeIndex);

  bool ReadCompressedPaths(const uint64_t ref_num_paths);

  bool ReadIndex(crate::Index *i);
  bool ReadString(std::string *s);
  bool ReadValueRep(crate::ValueRep *rep);

  bool ReadPathArray(std::vector<Path> *d);
  bool ReadStringArray(std::vector<std::string> *d);

  // customData(Dictionary)
  bool ReadCustomData(CustomDataType *d);

  bool ReadTimeSamples(value::TimeSamples *d);

  // integral array
  template <typename T>
  bool ReadIntArray(bool is_compressed, std::vector<T> *d);

  bool ReadHalfArray(bool is_compressed, std::vector<value::half> *d);
  bool ReadFloatArray(bool is_compressed, std::vector<float> *d);
  bool ReadDoubleArray(bool is_compressed, std::vector<double> *d);

  bool ReadPathListOp(ListOp<Path> *d);
  bool ReadTokenListOp(ListOp<value::token> *d);

  // Tracks the memory used(In advisorily manner since counting memory usage is
  // done by manually, so not all memory consumption could be tracked)
  size_t memory_used{0};  // in bytes.

  // Header(bootstrap)
  uint8_t _version[3] = {0, 0, 0};

  crate::TableOfContents _toc;

  int64_t _toc_offset{0};

  // index to _toc.sections
  int64_t _tokens_index{-1};
  int64_t _paths_index{-1};
  int64_t _strings_index{-1};
  int64_t _fields_index{-1};
  int64_t _fieldsets_index{-1};
  int64_t _specs_index{-1};

  std::vector<value::token> _tokens;
  std::vector<crate::Index> _string_indices;
  std::vector<crate::Field> _fields;
  std::vector<crate::Index> _fieldset_indices;
  std::vector<crate::Spec> _specs;
  std::vector<Path> _paths;
  std::vector<Path> _elemPaths; 

  std::vector<Node> _nodes;  // [0] = root node
                             //
  // `_live_fieldsets` contains unpacked value keyed by fieldset index.
  // Used for reconstructing Scene object
  // TODO(syoyo): Use unordered_map?
  std::map<crate::Index, FieldValuePairVector>
      _live_fieldsets;  // <fieldset index, List of field with unpacked Values>

  // class Impl;
  // Impl *_impl;

  const StreamReader *_sr{};

  void PushError(const std::string &s) const {
    _err += s;
  }
  void PushWarn(const std::string &s) const {
    _warn += s;
  }
  mutable std::string _err;
  mutable std::string _warn;

  CrateReaderConfig _config;
};

}  // namespace crate
}  // namespace tinyusdz
