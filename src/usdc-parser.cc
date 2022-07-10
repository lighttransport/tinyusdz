// SPDX-License-Identifier: MIT
// Copyright 2020-Present Syoyo Fujita.
//
// USDC(Crate) parser
//
// TODO:
//
// - [ ] Refactor Reconstruct*** function
// - [ ] Set `custom` in property by looking up schema.
// - [ ] And more...
//
#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"
#include "tinyusdz.hh"
#include "value-type.hh"
#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"
#include "crate-pprint.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "path-util.hh"
#include "pprinter.hh"
#include "stream-reader.hh"
#include "usdc-parser.hh"
#include "value-pprint.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

#ifdef TINYUSDZ_PRODUCTION_BUILD
// Do not include full filepath for privacy.
#define PUSH_ERROR(s)                                               \
  {                                                                 \
    std::ostringstream ss;                                          \
    ss << "[usdc-parser] " << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                        \
    _err += ss.str() + "\n";                                        \
  }                                                                 \
  while (0)

#define PUSH_WARN(s)                                                \
  {                                                                 \
    std::ostringstream ss;                                          \
    ss << "[usdc-parser] " << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                        \
    _warn += ss.str() + "\n";                                       \
  }                                                                 \
  while (0)
#else
#define PUSH_ERROR(s)                                              \
  {                                                                \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _err += ss.str() + "\n";                                       \
  }                                                                \
  while (0)

#define PUSH_WARN(s)                                               \
  {                                                                \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _warn += ss.str() + "\n";                                      \
  }                                                                \
  while (0)
#endif

#ifndef TINYUSDZ_PRODUCTION_BUILD
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x)                                               \
  do {                                                         \
    std::cout << __FILE__ << ":" << __func__ << ":"            \
              << std::to_string(__LINE__) << " " << x << "\n"; \
  } while (false)
#else
#define DCOUT(x)
#endif

namespace tinyusdz {
namespace usdc {

constexpr char kTypeName[] = "typeName";
constexpr char kToken[] = "Token";
constexpr char kDefault[] = "default";

template <class Int>
static inline bool ReadCompressedInts(const StreamReader *sr, Int *out,
                                      size_t size) {
  // TODO(syoyo): Error check
  using Compressor =
      typename std::conditional<sizeof(Int) == 4, Usd_IntegerCompression,
                                Usd_IntegerCompression64>::type;
  std::vector<char> compBuffer(Compressor::GetCompressedBufferSize(size));
  uint64_t compSize;
  if (!sr->read8(&compSize)) {
    return false;
  }

  if (!sr->read(size_t(compSize), size_t(compSize),
                reinterpret_cast<uint8_t *>(compBuffer.data()))) {
    return false;
  }
  std::string err;
  bool ret = Compressor::DecompressFromBuffer(
      compBuffer.data(), size_t(compSize), out, size, &err);
  (void)err;

  return ret;
}

static inline bool ReadIndices(const StreamReader *sr,
                               std::vector<crate::Index> *indices) {
  // TODO(syoyo): Error check
  uint64_t n;
  if (!sr->read8(&n)) {
    return false;
  }

  DCOUT("ReadIndices: n = " << n);

  indices->resize(size_t(n));
  size_t datalen = size_t(n) * sizeof(crate::Index);

  if (datalen != sr->read(datalen, datalen,
                          reinterpret_cast<uint8_t *>(indices->data()))) {
    return false;
  }

  return true;
}

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

  NodeType GetNodeType() const { return _node_type; }

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

  NodeType _node_type;
};

class Parser::Impl {
 public:
  Impl(StreamReader *sr, int num_threads) : _sr(sr) {
    if (num_threads == -1) {
#if defined(__wasi__)
      num_threads = 1;
#else
      num_threads = std::max(1, int(std::thread::hardware_concurrency()));
#endif
    }

    // Limit to 1024 threads.
    _num_threads = std::min(1024, num_threads);
  }

  bool ReadBootStrap();
  bool ReadTOC();

  // Read known sections
  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();

  ///
  /// Read TOC section
  ///
  bool ReadSection(crate::Section *s);

  const value::token GetToken(crate::Index token_index) {
    if (token_index.value <= _tokens.size()) {
      return _tokens[token_index.value];
    } else {
      _err += "Token index out of range: " + std::to_string(token_index.value) +
              "\n";
      return value::token();
    }
  }

  const value::token GetToken(crate::Index token_index) const {
    if (token_index.value <= _tokens.size()) {
      return _tokens[token_index.value];
    } else {
      return value::token();
    }
  }

  // Get string token from string index.
  const value::token GetStringToken(crate::Index string_index) {
    if (string_index.value <= _string_indices.size()) {
      crate::Index s_idx = _string_indices[string_index.value];
      return GetToken(s_idx);
    } else {
      _err +=
          "String index out of range: " + std::to_string(string_index.value) +
          "\n";
      return value::token();
    }
  }

  bool HasField(const std::string &key) const {
    // Simple linear search
    for (const auto &field : _fields) {
      const value::token field_name = GetToken(field.token_index);
      if (field_name.str().compare(key) == 0) {
        return true;
      }
    }
    return false;
  }

  bool GetField(crate::Index index, crate::Field &&field) const {
    if (index.value <= _fields.size()) {
      field = _fields[index.value];
      return true;
    } else {
      return false;
    }
  }

  std::string GetFieldString(crate::Index index) {
    if (index.value <= _fields.size()) {
      // ok
    } else {
      return "#INVALID field index#";
    }

    const crate::Field &f = _fields[index.value];

    std::string s =
        GetToken(f.token_index).str() + ":" + f.value_rep.GetStringRepr();

    return s;
  }

  Path GetPath(crate::Index index) {
    if (index.value <= _paths.size()) {
      // ok
    } else {
      PUSH_ERROR("Invalid path index?");
      return Path();
    }

    const Path &p = _paths[index.value];

    return p;
  }

  std::string GetPathString(crate::Index index) {
    if (index.value <= _paths.size()) {
      // ok
    } else {
      PUSH_ERROR("Invalid path index");
      return "#INVALID path index#";
    }

    const Path &p = _paths[index.value];

    return p.full_path_name();
  }

  std::string GetSpecString(crate::Index index) {
    if (index.value <= _specs.size()) {
      // ok
    } else {
      PUSH_ERROR("Invalid path index");
      return "#INVALID spec index#";
    }

    const crate::Spec &spec = _specs[index.value];

    std::string path_str = GetPathString(spec.path_index);
    std::string specty_str = to_string(spec.spec_type);

    return "[Spec] path: " + path_str +
           ", fieldset id: " + std::to_string(spec.fieldset_index.value) +
           ", spec_type: " + specty_str;
  }

  ///
  /// Methods for reconstructing `Scene` object
  ///

  // In-memory storage for a single "spec" -- prim, property, etc.
  typedef std::pair<std::string, crate::CrateValue> FieldValuePair;
  typedef std::vector<FieldValuePair> FieldValuePairVector;

  ///
  /// Find if a field with (`name`, `tyname`) exists in FieldValuePairVector.
  ///
  bool HasFieldValuePair(const FieldValuePairVector &fvs,
                         const std::string &name, const std::string &tyname) {
    for (const auto &fv : fvs) {
      if ((fv.first == name) && (fv.second.GetTypeName() == tyname)) {
        return true;
      }
    }

    return false;
  }

  ///
  /// Find if a field with `name`(type can be arbitrary) exists in
  /// FieldValuePairVector.
  ///
  bool HasFieldValuePair(const FieldValuePairVector &fvs,
                         const std::string &name) {
    for (const auto &fv : fvs) {
      if (fv.first == name) {
        return true;
      }
    }

    return false;
  }

  nonstd::expected<FieldValuePair, std::string> GetFieldValuePair(
      const FieldValuePairVector &fvs, const std::string &name,
      const std::string &tyname) {
    for (const auto &fv : fvs) {
      if ((fv.first == name) && (fv.second.GetTypeName() == tyname)) {
        return fv;
      }
    }

    return nonstd::make_unexpected("FieldValuePair not found with name: `" +
                                   name + "` and specified type: `" + tyname +
                                   "`");
  }

  nonstd::expected<FieldValuePair, std::string> GetFieldValuePair(
      const FieldValuePairVector &fvs, const std::string &name) {
    for (const auto &fv : fvs) {
      if (fv.first == name) {
        return fv;
      }
    }

    return nonstd::make_unexpected("FieldValuePair not found with name: `" +
                                   name + "`");
  }

  // `_live_fieldsets` contains unpacked value keyed by fieldset index.
  // Used for reconstructing Scene object
  // TODO(syoyo): Use unordered_map(need hash function)
  std::map<crate::Index, FieldValuePairVector>
      _live_fieldsets;  // <fieldset index, List of field with unpacked Values>

  bool BuildLiveFieldSets();

  ///
  /// Parse node's attribute from FieldValuePairVector.
  ///
  bool ParseAttribute(const FieldValuePairVector &fvs, PrimAttrib *attr,
                      const std::string &prop_name);

  bool ReconstructXform(const Node &node, const FieldValuePairVector &fields,
                        const std::unordered_map<uint32_t, uint32_t>
                            &path_index_to_spec_index_map,
                        Xform *xform);

  bool ReconstructGeomSubset(const Node &node,
                             const FieldValuePairVector &fields,
                             const std::unordered_map<uint32_t, uint32_t>
                                 &path_index_to_spec_index_map,
                             GeomSubset *mesh);

  bool ReconstructGeomMesh(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           GeomMesh *mesh);

  bool ReconstructGeomBasisCurves(const Node &node,
                                  const FieldValuePairVector &fields,
                                  const std::unordered_map<uint32_t, uint32_t>
                                      &path_index_to_spec_index_map,
                                  GeomBasisCurves *curves);

  bool ReconstructMaterial(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           Material *material);

  bool ReconstructShader(const Node &node, const FieldValuePairVector &fields,
                         const std::unordered_map<uint32_t, uint32_t>
                             &path_index_to_spec_index_map,
                         Shader *shader);

  bool ReconstructPreviewSurface(const Node &node,
                                 const FieldValuePairVector &fields,
                                 const std::unordered_map<uint32_t, uint32_t>
                                     &path_index_to_spec_index_map,
                                 PreviewSurface *surface);

  bool ReconstructUVTexture(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            UVTexture *uvtex);

  bool ReconstructPrimvarReader_float2(
      const Node &node, const FieldValuePairVector &fields,
      const std::unordered_map<uint32_t, uint32_t>
          &path_index_to_spec_index_map,
      PrimvarReader_float2 *preader);

  bool ReconstructSkelRoot(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           SkelRoot *skelRoot);

  bool ReconstructSkeleton(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           Skeleton *skeleton);

  bool ReconstructSceneRecursively(int parent_id, int level,
                                   const std::unordered_map<uint32_t, uint32_t>
                                       &path_index_to_spec_index_map,
                                   Scene *scene);

  bool ReconstructScene(Scene *scene);

  ///
  /// --------------------------------------------------
  ///

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const { return memory_used / (1024 * 1024); }

  //
  // APIs valid after successfull Parse()
  //

  size_t NumPaths() const { return _paths.size(); }

 private:
  bool ReadCompressedPaths(const uint64_t ref_num_paths);

  const StreamReader *_sr = nullptr;
  std::string _err;
  std::string _warn;

  int _num_threads{1};

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

  std::vector<Node> _nodes;  // [0] = root node

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

  //
  // Reader util functions
  //
  bool ReadIndex(crate::Index *i);

  // bool ReadToken(std::string *s);
  bool ReadString(std::string *s);

  bool ReadValueRep(crate::ValueRep *rep);

  bool ReadPathArray(std::vector<Path> *d);
  bool ReadStringArray(std::vector<std::string> *d);

  // Dictionary
  bool ReadDictionary(crate::CrateValue::Dictionary *d);

  bool ReadTimeSamples(value::TimeSamples *d);

  // integral array
  template <typename T>
  bool ReadIntArray(bool is_compressed, std::vector<T> *d);

  bool ReadHalfArray(bool is_compressed, std::vector<value::half> *d);
  bool ReadFloatArray(bool is_compressed, std::vector<float> *d);
  bool ReadDoubleArray(bool is_compressed, std::vector<double> *d);

  bool ReadPathListOp(ListOp<Path> *d);
  bool ReadTokenListOp(ListOp<value::token> *d);
  //bool ReadReferenceListOp(ListOp<Referene> *d);
};

//
// -- Impl
//
bool Parser::Impl::ReadIndex(crate::Index *i) {
  // string is serialized as StringIndex
  uint32_t value;
  if (!_sr->read4(&value)) {
    PUSH_ERROR("Failed to read Index");
    return false;
  }
  (*i) = crate::Index(value);
  return true;
}

bool Parser::Impl::ReadString(std::string *s) {
  // string is serialized as StringIndex
  crate::Index string_index;
  if (!ReadIndex(&string_index)) {
    _err += "Failed to read Index for string data.\n";
    return false;
  }

  (*s) = GetStringToken(string_index).str();

  return true;
}

bool Parser::Impl::ReadValueRep(crate::ValueRep *rep) {
  if (!_sr->read8(reinterpret_cast<uint64_t *>(rep))) {
    _err += "Failed to read ValueRep.\n";
    return false;
  }

  DCOUT("value = " << rep->GetData());

  return true;
}

template <typename T>
bool Parser::Impl::ReadIntArray(bool is_compressed, std::vector<T> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        // PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        //_err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(T) * length, sizeof(T) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      //_err += "Failed to read integer array data.\n";
      return false;
    }

    return true;

  } else {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        //_err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        //_err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    DCOUT("array.len = " << length);

    d->resize(length);

    if (length < crate::kMinCompressedArraySize) {
      size_t sz = sizeof(T) * length;
      // Not stored in compressed.
      // reader.ReadContiguous(odata, osize);
      if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
        PUSH_ERROR("Failed to read uncompressed array data.");
        return false;
      }
      return true;
    }

    return ReadCompressedInts(_sr, d->data(), d->size());
  }
}

bool Parser::Impl::ReadHalfArray(bool is_compressed,
                                 std::vector<value::half> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(uint16_t) * length, sizeof(uint16_t) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read half array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

  DCOUT("array.len = " << length);

  d->resize(length);

  if (length < crate::kMinCompressedArraySize) {
    size_t sz = sizeof(uint16_t) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  // Read the code
  char code;
  if (!_sr->read1(&code)) {
    _err += "Failed to read the code.\n";
    return false;
  }

  if (code == 'i') {
    // Compressed integers.
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadHalfArray.\n";
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      float f = float(ints[i]);
      value::half h = float_to_half_full(f);
      (*d)[i] = h;
    }
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadHalfArray.\n";
      return false;
    }

    std::vector<value::half> lut(lutSize);
    if (!_sr->read(sizeof(value::half) * lutSize, sizeof(value::half) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadHalfArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
      _err += "Failed to read lut indices in ReadHalfArray.\n";
      return false;
    }

    auto o = d->data();
    for (auto index : indexes) {
      *o++ = lut[index];
    }
  } else {
    _err += "Invalid code. Data is currupted\n";
    return false;
  }

  return true;
}

bool Parser::Impl::ReadFloatArray(bool is_compressed, std::vector<float> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(float) * length, sizeof(float) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read float array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

  DCOUT("array.len = " << length);

  d->resize(length);

  if (length < crate::kMinCompressedArraySize) {
    size_t sz = sizeof(float) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  // Read the code
  char code;
  if (!_sr->read1(&code)) {
    _err += "Failed to read the code.\n";
    return false;
  }

  if (code == 'i') {
    // Compressed integers.
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadFloatArray.\n";
      return false;
    }
    std::copy(ints.begin(), ints.end(), d->data());
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadFloatArray.\n";
      return false;
    }

    std::vector<float> lut(lutSize);
    if (!_sr->read(sizeof(float) * lutSize, sizeof(float) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadFloatArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
      _err += "Failed to read lut indices in ReadFloatArray.\n";
      return false;
    }

    auto o = d->data();
    for (auto index : indexes) {
      *o++ = lut[index];
    }
  } else {
    _err += "Invalid code. Data is currupted\n";
    return false;
  }

  return true;
}

bool Parser::Impl::ReadDoubleArray(bool is_compressed, std::vector<double> *d) {
  if (!is_compressed) {
    size_t length;
    // < ver 0.7.0  use 32bit
    if ((_version[0] == 0) && ((_version[1] < 7))) {
      uint32_t n;
      if (!_sr->read4(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }
      length = size_t(n);
    } else {
      uint64_t n;
      if (!_sr->read8(&n)) {
        _err += "Failed to read the number of array elements.\n";
        return false;
      }

      length = size_t(n);
    }

    d->resize(length);

    // TODO(syoyo): Zero-copy
    if (!_sr->read(sizeof(double) * length, sizeof(double) * length,
                   reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read double array data.\n";
      return false;
    }

    return true;
  }

  //
  // compressed data is represented by integers or look-up table.
  //

  size_t length;
  // < ver 0.7.0  use 32bit
  if ((_version[0] == 0) && ((_version[1] < 7))) {
    uint32_t n;
    if (!_sr->read4(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }
    length = size_t(n);
  } else {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read the number of array elements.\n";
      return false;
    }

    length = size_t(n);
  }

  DCOUT("array.len = " << length);

  d->resize(length);

  if (length < crate::kMinCompressedArraySize) {
    size_t sz = sizeof(double) * length;
    // Not stored in compressed.
    // reader.ReadContiguous(odata, osize);
    if (!_sr->read(sz, sz, reinterpret_cast<uint8_t *>(d->data()))) {
      _err += "Failed to read uncompressed array data.\n";
      return false;
    }
    return true;
  }

  // Read the code
  char code;
  if (!_sr->read1(&code)) {
    _err += "Failed to read the code.\n";
    return false;
  }

  if (code == 'i') {
    // Compressed integers.
    std::vector<int32_t> ints(length);
    if (!ReadCompressedInts(_sr, ints.data(), ints.size())) {
      _err += "Failed to read compressed ints in ReadDoubleArray.\n";
      return false;
    }
    std::copy(ints.begin(), ints.end(), d->data());
  } else if (code == 't') {
    // Lookup table & indexes.
    uint32_t lutSize;
    if (!_sr->read4(&lutSize)) {
      _err += "Failed to read lutSize in ReadDoubleArray.\n";
      return false;
    }

    std::vector<double> lut(lutSize);
    if (!_sr->read(sizeof(double) * lutSize, sizeof(double) * lutSize,
                   reinterpret_cast<uint8_t *>(lut.data()))) {
      _err += "Failed to read lut table in ReadDoubleArray.\n";
      return false;
    }

    std::vector<uint32_t> indexes(length);
    if (!ReadCompressedInts(_sr, indexes.data(), indexes.size())) {
      _err += "Failed to read lut indices in ReadDoubleArray.\n";
      return false;
    }

    auto o = d->data();
    for (auto index : indexes) {
      *o++ = lut[index];
    }
  } else {
    _err += "Invalid code. Data is currupted\n";
    return false;
  }

  return true;
}

bool Parser::Impl::ReadTimeSamples(value::TimeSamples *d) {
  (void)d;

  // TODO(syoyo): Deferred loading of TimeSamples?(See USD's implementation)

  DCOUT("ReadTimeSamples: offt before tell = " << _sr->tell());

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // crateFile.cpp for details.
  int64_t offset{0};
  if (!_sr->read8(&offset)) {
    _err += "Failed to read the offset for value in Dictionary.\n";
    return false;
  }

  DCOUT("TimeSample times value offset = " << offset);
  DCOUT("TimeSample tell = " << _sr->tell());

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    _err += "Failed to seek to TimeSample times. Invalid offset value: " +
            std::to_string(offset) + "\n";
    return false;
  }

  // TODO(syoyo): Deduplicate times?

  crate::ValueRep rep{0};
  if (!ReadValueRep(&rep)) {
    _err += "Failed to read ValueRep for TimeSample' times element.\n";
    return false;
  }

  // Save offset
  size_t values_offset = _sr->tell();

  crate::CrateValue value;
  if (!UnpackValueRep(rep, &value)) {
    _err += "Failed to unpack value of TimeSample's times element.\n";
    return false;
  }

  // must be an array of double.
  DCOUT("TimeSample times:" << value.GetTypeName());
  DCOUT("TODO: Parse TimeSample values");

  //
  // Parse values for TimeSamples.
  // TODO(syoyo): Delayed loading of values.
  //

  // seek position will be changed in `_UnpackValueRep`, so revert it.
  if (!_sr->seek_set(values_offset)) {
    _err += "Failed to seek to TimeSamples values.\n";
    return false;
  }

  // 8byte for the offset for recursive value. See RecursiveRead() in
  // crateFile.cpp for details.
  if (!_sr->read8(&offset)) {
    _err += "Failed to read the offset for value in TimeSamples.\n";
    return false;
  }

  DCOUT("TimeSample value offset = " << offset);
  DCOUT("TimeSample tell = " << _sr->tell());

  // -8 to compensate sizeof(offset)
  if (!_sr->seek_from_current(offset - 8)) {
    _err += "Failed to seek to TimeSample values. Invalid offset value: " +
            std::to_string(offset) + "\n";
    return false;
  }

  uint64_t num_values{0};
  if (!_sr->read8(&num_values)) {
    _err += "Failed to read the number of values from TimeSamples.\n";
    return false;
  }

  DCOUT("Number of values = " << num_values);

  _warn += "TODO: Decode TimeSample's values\n";

  // Move to next location.
  // sizeof(uint64) = sizeof(ValueRep)
  if (!_sr->seek_from_current(int64_t(sizeof(uint64_t) * num_values))) {
    _err += "Failed to seek over TimeSamples's values.\n";
    return false;
  }

  return true;
}

bool Parser::Impl::ReadStringArray(std::vector<std::string> *d) {
  // array data is not compressed
  auto ReadFn = [this](std::vector<std::string> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read STRING_VECTOR data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetStringToken(ivalue[i]).str();
    }

    return true;
  };

  std::vector<std::string> items;
  if (!ReadFn(items)) {
    _err += "Failed to read String vector.\n";
    return false;
  }

  (*d) = items;

  return true;
}

bool Parser::Impl::ReadPathArray(std::vector<Path> *d) {
  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetPath(ivalue[i]);
    }

    return true;
  };

  std::vector<Path> items;
  if (!ReadFn(items)) {
    _err += "Failed to read Path vector.\n";
    return false;
  }

  (*d) = items;

  return true;
}

bool Parser::Impl::ReadTokenListOp(ListOp<value::token> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<value::token> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetToken(ivalue[i]);
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<value::token> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}

bool Parser::Impl::ReadPathListOp(ListOp<Path> *d) {
  // read ListOpHeader
  ListOpHeader h;
  if (!_sr->read1(&h.bits)) {
    _err += "Failed to read ListOpHeader\n";
    return false;
  }

  if (h.IsExplicit()) {
    d->ClearAndMakeExplicit();
  }

  // array data is not compressed
  auto ReadFn = [this](std::vector<Path> &result) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      _err += "Failed to read # of elements in ListOp.\n";
      return false;
    }

    std::vector<crate::Index> ivalue(static_cast<size_t>(n));

    if (!_sr->read(size_t(n) * sizeof(crate::Index),
                   size_t(n) * sizeof(crate::Index),
                   reinterpret_cast<uint8_t *>(ivalue.data()))) {
      _err += "Failed to read ListOp data.\n";
      return false;
    }

    // reconstruct
    result.resize(static_cast<size_t>(n));
    for (size_t i = 0; i < n; i++) {
      result[i] = GetPath(ivalue[i]);
    }

    return true;
  };

  if (h.HasExplicitItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::ExplicitItems.\n";
      return false;
    }

    d->SetExplicitItems(items);
  }

  if (h.HasAddedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AddedItems.\n";
      return false;
    }

    d->SetAddedItems(items);
  }

  if (h.HasPrependedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::PrependedItems.\n";
      return false;
    }

    d->SetPrependedItems(items);
  }

  if (h.HasAppendedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::AppendedItems.\n";
      return false;
    }

    d->SetAppendedItems(items);
  }

  if (h.HasDeletedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::DeletedItems.\n";
      return false;
    }

    d->SetDeletedItems(items);
  }

  if (h.HasOrderedItems()) {
    std::vector<Path> items;
    if (!ReadFn(items)) {
      _err += "Failed to read ListOp::OrderedItems.\n";
      return false;
    }

    d->SetOrderedItems(items);
  }

  return true;
}

bool Parser::Impl::ReadDictionary(crate::CrateValue::Dictionary *d) {
  crate::CrateValue::Dictionary dict;
  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for Dictionary data.\n";
    return false;
  }

  DCOUT("# o elements in dict" << sz);

  while (sz--) {
    // key(StringIndex)
    std::string key;

    if (!ReadString(&key)) {
      _err += "Failed to read key string for Dictionary element.\n";
      return false;
    }

    // 8byte for the offset for recursive value. See RecursiveRead() in
    // crateFile.cpp for details.
    int64_t offset{0};
    if (!_sr->read8(&offset)) {
      _err += "Failed to read the offset for value in Dictionary.\n";
      return false;
    }

    // -8 to compensate sizeof(offset)
    if (!_sr->seek_from_current(offset - 8)) {
      _err +=
          "Failed to seek. Invalid offset value: " + std::to_string(offset) +
          "\n";
      return false;
    }

    DCOUT("key = " << key);

    crate::ValueRep rep{0};
    if (!ReadValueRep(&rep)) {
      _err += "Failed to read value for Dictionary element.\n";
      return false;
    }

    DCOUT("vrep =" << crate::GetCrateDataTypeName(rep.GetType()));

    size_t saved_position = _sr->tell();

    crate::CrateValue value;
    if (!UnpackValueRep(rep, &value)) {
      _err += "Failed to unpack value of Dictionary element.\n";
      return false;
    }

    dict[key] = value;

    if (!_sr->seek_set(saved_position)) {
      _err += "Failed to set seek in ReadDict\n";
      return false;
    }
  }

  (*d) = std::move(dict);
  return true;
}

bool Parser::Impl::UnpackInlinedValueRep(const crate::ValueRep &rep,
                                         crate::CrateValue *value) {
  if (!rep.IsInlined()) {
    PUSH_ERROR("ValueRep must be inlined value representation.");
    return false;
  }

  const auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
    return false;
  }

  if (rep.IsCompressed()) {
    PUSH_ERROR("Inlinved value must not be compressed.");
    return false;
  }

  if (rep.IsArray()) {
    PUSH_ERROR("Inlined value must not be an array.");
    return false;
  }

  const auto dty = tyRet.value();
  DCOUT(crate::GetCrateDataTypeRepr(dty));

  uint32_t d = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
  DCOUT("d = " << d);

  // TODO(syoyo): Use template SFINE?
  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      value->Set(d ? true : false);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      // AssetPath = std::string(storage format is TokenIndex).
      std::string str = GetToken(crate::Index(d)).str();

      value::asset_path assetp(str);
      value->Set(assetp);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      value::token tok = GetToken(crate::Index(d));

      DCOUT("value.token = " << tok);

      value->Set(tok);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      std::string str = GetStringToken(crate::Index(d)).str();
      DCOUT("value.string = " << str);

      value->Set(str);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER: {
      if (d >= static_cast<int>(Specifier::Invalid)) {
        _err += "Invalid value for Specifier\n";
        return false;
      }
      Specifier val = static_cast<Specifier>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION: {
      if (d >= static_cast<int>(Permission::Invalid)) {
        _err += "Invalid value for Permission\n";
        return false;
      }
      Permission val = static_cast<Permission>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY: {
      if (d >= static_cast<int>(Variability::Invalid)) {
        _err += "Invalid value for Variability\n";
        return false;
      }
      Variability val = static_cast<Variability>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      uint8_t val;
      memcpy(&val, &d, 1);

      DCOUT("value.uchar = " << val);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      int ival;
      memcpy(&ival, &d, sizeof(int));

      DCOUT("value.int = " << ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      uint32_t val;
      memcpy(&val, &d, sizeof(uint32_t));

      DCOUT("value.uint = " << val);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      // stored as int
      int _ival;
      memcpy(&_ival, &d, sizeof(int));

      DCOUT("value.int = " << _ival);

      int64_t ival = static_cast<int64_t>(_ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {
      // stored as uint32
      uint32_t _ival;
      memcpy(&_ival, &d, sizeof(uint32_t));

      DCOUT("value.int = " << _ival);

      uint64_t ival = static_cast<uint64_t>(_ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      value::half f;
      memcpy(&f, &d, sizeof(value::half));

      DCOUT("value.half = " << f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      float f;
      memcpy(&f, &d, sizeof(float));

      DCOUT("value.float = " << f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      // stored as float
      float _f;
      memcpy(&_f, &d, sizeof(float));

      double f = static_cast<double>(_f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::matrix2d v;
      memset(v.m, 0, sizeof(value::matrix2d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << "\n");

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::matrix3d v;
      memset(v.m, 0, sizeof(value::matrix3d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << ", "
                                    << v.m[2][2] << "\n");

      value->Set(v);

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::matrix4d v;
      memset(v.m, 0, sizeof(value::matrix4d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);
      v.m[3][3] = static_cast<double>(data[3]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << ", "
                                    << v.m[2][2] << ", " << v.m[3][3]);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      // Seems quaternion type is not allowed for Inlined Value.
      PUSH_ERROR("Quaternion type is not allowed for Inlined Value.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::half3 v;
      v[0] = float_to_half_full(float(data[0]));
      v[1] = float_to_half_full(float(data[1]));
      v[2] = float_to_half_full(float(data[2]));

      DCOUT("value.half3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::int3 v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);

      DCOUT("value.int3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::double4 v;
      v[0] = static_cast<double>(data[0]);
      v[1] = static_cast<double>(data[1]);
      v[2] = static_cast<double>(data[2]);
      v[3] = static_cast<double>(data[3]);

      DCOUT("value.doublef = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::float4 v;
      v[0] = static_cast<float>(data[0]);
      v[1] = static_cast<float>(data[1]);
      v[2] = static_cast<float>(data[2]);
      v[3] = static_cast<float>(data[3]);

      DCOUT("value.vec4f = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::half4 v;
      v[0] = float_to_half_full(float(data[0]));
      v[1] = float_to_half_full(float(data[0]));
      v[2] = float_to_half_full(float(data[0]));
      v[3] = float_to_half_full(float(data[0]));

      DCOUT("value.vec4h = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::int4 v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);
      v[3] = static_cast<int32_t>(data[3]);

      DCOUT("value.vec4i = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY: {
      // empty dict is allowed
      // TODO: empty(zero value) check?
      crate::CrateValue::Dictionary dict;
      value->Set(dict);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Inlined value: " +
          crate::GetCrateDataTypeName(dty.dtype_id));
      return false;
    }
  }

  // Should never reach here.
  return false;
}

#if 0
template<T>
Parser::Impl::UnpackArrayValue(CrateDataTypeId dty, crate::CrateValue *value_out) {
  uint64_t n;
  if (!_sr->read8(&n)) {
    PUSH_ERROR("Failed to read the number of array elements.");
    return false;
  }

  std::vector<crate::Index> v(static_cast<size_t>(n));
  if (!_sr->read(size_t(n) * sizeof(crate::Index),
                 size_t(n) * sizeof(crate::Index),
                 reinterpret_cast<uint8_t *>(v.data()))) {
    PUSH_ERROR("Failed to read array data.");
    return false;
  }

  return true;
}
#endif

bool Parser::Impl::UnpackValueRep(const crate::ValueRep &rep,
                                  crate::CrateValue *value) {
  if (rep.IsInlined()) {
    return UnpackInlinedValueRep(rep, value);
  }

  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
  }

  const auto dty = tyRet.value();

#define TODO_IMPLEMENT(__dty)                                     \
  { \
  PUSH_ERROR("TODO: '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
             "' data is not yet implemented.");                               \
  return false;                                                             \
  }

#define COMPRESS_UNSUPPORTED_CHECK(__dty)                                     \
  if (rep.IsCompressed()) {                                                   \
    PUSH_ERROR("Compressed [" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet supported.");                               \
    return false;                                                             \
  }

#define NON_ARRAY_UNSUPPORTED_CHECK(__dty)                                   \
  if (!rep.IsArray()) {                                                       \
    PUSH_ERROR("Non array '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet supported.");                              \
    return false;                                                            \
  }

#define ARRAY_UNSUPPORTED_CHECK(__dty)                                   \
  if (rep.IsArray()) {                                                       \
    PUSH_ERROR("Array of '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data type is not yet supported.");                              \
    return false;                                                            \
  }


  // payload is the offset to data.
  uint64_t offset = rep.GetPayload();
  if (!_sr->seek_set(offset)) {
    PUSH_ERROR("Invalid offset.");
    return false;
  }

  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {

      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        TODO_IMPLEMENT(dty)
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {

      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        // AssetPath = std::string(storage format is TokenIndex).
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<value::asset_path> apaths(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          DCOUT("Token[" << i << "] = " << GetToken(v[i]) << " (" << v[i].value
                         << ")");
          apaths[i] = value::asset_path(GetToken(v[i]).str());
        }

        value->Set(apaths);
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {

      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<value::token> tokens(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          DCOUT("Token[" << i << "] = " << GetToken(v[i]) << " (" << v[i].value
                         << ")");
          tokens[i] = GetToken(v[i]);
        }

        value->Set(tokens);
        return true;
      } else {

        return false;

      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<std::string> stringArray(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          stringArray[i] = GetStringToken(v[i]).str();
        }

        DCOUT("stringArray = " << stringArray);

        // TODO: Use token type?
        value->Set(stringArray);

        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY:
    {
      PUSH_ERROR("TODO: Specifier/Permission/Variability. isArray " << rep.IsArray() << ", isCompressed " << rep.IsCompressed());
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)
      TODO_IMPLEMENT(dty)
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<int32_t> v;
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int array.");
          return false;
        }

        DCOUT("IntArray = " << v);

        value->Set(v);
        return true;
      } else {
        return false;
      }

    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<uint32_t> v;
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint array.");
          return false;
        }

        DCOUT("UIntArray = " << v);

        value->Set(v);
        return true;
      } else {
        return false;
      }

    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      if (rep.IsArray()) {
        std::vector<int64_t> v;
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int64 array.");
          return false;
        }

        DCOUT("Int64Array = " << v);

        value->Set(v);
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        int64_t v;
        if (!_sr->read(sizeof(int64_t), sizeof(int64_t),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int64 data.");
          return false;
        }

        DCOUT("int64 = " << v);

        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {

      if (rep.IsArray()) {
        std::vector<uint64_t> v;
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint64 array.");
          return false;
        }

        DCOUT("UInt64Array = " << v);

        value->Set(v);
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        uint64_t v;
        if (!_sr->read(sizeof(uint64_t), sizeof(uint64_t),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read uint64 data.");
          return false;
        }

        DCOUT("uint64 = " << v);

        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {

      if (rep.IsArray()) {
        std::vector<value::half> v;
        if (!ReadHalfArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read half array value.");
          return false;
        }

        value->Set(v);

        return true;
      } else {

        PUSH_ERROR("Non-inlined, non-array Half value is invalid.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {

      if (rep.IsArray()) {
        std::vector<float> v;
        if (!ReadFloatArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read float array value.");
          return false;
        }

        DCOUT("FloatArray = " << v);

        value->Set(v);

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        PUSH_ERROR("Non-inlined, non-array Float value is not supported.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      if (rep.IsArray()) {
        std::vector<double> v;
        if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("DoubleArray = " << v);
        value->Set(v);

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        double v;
        if (!_sr->read_double(&v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("Double " << v);

        value->Set(v);

        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::matrix2d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix2d),
                       size_t(n) * sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix2d array.");
          return false;
        }

        value->Set(v);

      } else {
        static_assert(sizeof(value::matrix2d) == (8 * 4), "");

        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix2d), sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix2d` type\n";
          return false;
        }

        DCOUT("value.matrix2d = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::matrix3d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix3d),
                       size_t(n) * sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix3d array.");
          return false;
        }

        value->Set(v);

      } else {
        static_assert(sizeof(value::matrix3d) == (8 * 9), "");

        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix3d), sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix3d` type\n";
          return false;
        }

        DCOUT("value.matrix3d = " << v);

        value->Set(v);
      }

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::matrix4d> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix4d),
                       size_t(n) * sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix4d array.");
          return false;
        }

        value->Set(v);

      } else {
        static_assert(sizeof(value::matrix4d) == (8 * 16), "");

        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix4d), sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix4d` type\n";
          return false;
        }

        DCOUT("value.matrix4d = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD: {
      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::quatd> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatd), size_t(n) * sizeof(value::quatd),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(v);

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        value::quatd v;
        if (!_sr->read(sizeof(value::quatd), sizeof(value::quatd),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatd value\n";
          return false;
        }

        DCOUT("Quatd = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF: {

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::quatf> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatf), size_t(n) * sizeof(value::quatf),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(v);

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        value::quatf v;
        if (!_sr->read(sizeof(value::quatf), sizeof(value::quatf),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatf value\n";
          return false;
        }

        DCOUT("Quatf = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::quath> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quath), size_t(n) * sizeof(value::quath),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quath array.");
          return false;
        }

        DCOUT("Quath[] = " << v);

        value->Set(v);

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        value::quath v;
        if (!_sr->read(sizeof(value::quath), sizeof(value::quath),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quath value\n";
          return false;
        }

        DCOUT("Quath = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::double2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double2), size_t(n) * sizeof(value::double2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double2 array.");
          return false;
        }

        DCOUT("double2 = " << v);

        value->Set(v);
        return true;
      } else {
        value::double2 v;
        if (!_sr->read(sizeof(value::double2), sizeof(value::double2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double2 data.");
          return false;
        }

        DCOUT("double2 = " << v);

        value->Set(v);
        return true;
      }
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::float2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float2), size_t(n) * sizeof(value::float2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float2 array.");
          return false;
        }

        DCOUT("float2 = " << v);

        value->Set(v);
        return true;
      } else {
        value::float2 v;
        if (!_sr->read(sizeof(value::float2), sizeof(value::float2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float2 data.");
          return false;
        }

        DCOUT("float2 = " << v);

        value->Set(v);
        return true;
      }
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::half2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half2), size_t(n) * sizeof(value::half2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half2 array.");
          return false;
        }

        DCOUT("half2 = " << v);
        value->Set(v);

      } else {
        value::half2 v;
        if (!_sr->read(sizeof(value::half2), sizeof(value::half2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half2");
          return false;
        }

        DCOUT("half2 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::int2> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int2), size_t(n) * sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int2 array.");
          return false;
        }

        DCOUT("int2 = " << v);
        value->Set(v);

      } else {
        value::int2 v;
        if (!_sr->read(sizeof(value::int2), sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int2");
          return false;
        }

        DCOUT("int2 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::double3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double3), size_t(n) * sizeof(value::double3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double3 array.");
          return false;
        }

        DCOUT("double3 = " << v);
        value->Set(v);

      } else {
        value::double3 v;
        if (!_sr->read(sizeof(value::double3), sizeof(value::double3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double3");
          return false;
        }

        DCOUT("double3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::float3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float3), size_t(n) * sizeof(value::float3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float3 array.");
          return false;
        }

        DCOUT("float3f = " << v);
        value->Set(v);

      } else {
        value::float3 v;
        if (!_sr->read(sizeof(value::float3), sizeof(value::float3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float3");
          return false;
        }

        DCOUT("float3 = " << v);

        value->Set(v);
      }

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::half3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half3), size_t(n) * sizeof(value::half3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half3 array.");
          return false;
        }

        DCOUT("half3 = " << v);
        value->Set(v);

      } else {
        value::half3 v;
        if (!_sr->read(sizeof(value::half3), sizeof(value::half3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half3");
          return false;
        }

        DCOUT("half3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::int3> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int3), size_t(n) * sizeof(value::int3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int3 array.");
          return false;
        }

        DCOUT("int3 = " << v);
        value->Set(v);

      } else {
        value::int3 v;
        if (!_sr->read(sizeof(value::int3), sizeof(value::int3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int3");
          return false;
        }

        DCOUT("int3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::double4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double4), size_t(n) * sizeof(value::double4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double4 array.");
          return false;
        }

        DCOUT("double4 = " << v);
        value->Set(v);

      } else {
        value::double4 v;
        if (!_sr->read(sizeof(value::double4), sizeof(value::double4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double4");
          return false;
        }

        DCOUT("double4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::float4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float4), size_t(n) * sizeof(value::float4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float4 array.");
          return false;
        }

        DCOUT("float4 = " << v);
        value->Set(v);

      } else {
        value::float4 v;
        if (!_sr->read(sizeof(value::float4), sizeof(value::float4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float4");
          return false;
        }

        DCOUT("float4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::half4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half4), size_t(n) * sizeof(value::half4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half4 array.");
          return false;
        }

        DCOUT("half4 = " << v);
        value->Set(v);

      } else {
        value::half4 v;
        if (!_sr->read(sizeof(value::half4), sizeof(value::half4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half4");
          return false;
        }

        DCOUT("half4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        std::vector<value::int4> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int4), size_t(n) * sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int4 array.");
          return false;
        }

        DCOUT("int4 = " << v);
        value->Set(v);

      } else {
        value::int4 v;
        if (!_sr->read(sizeof(value::int4), sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int4");
          return false;
        }

        DCOUT("int4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      ARRAY_UNSUPPORTED_CHECK(dty)

      crate::CrateValue::Dictionary dict;

      if (!ReadDictionary(&dict)) {
        _err += "Failed to read Dictionary value\n";
        return false;
      }

      DCOUT("Dict. nelems = " << dict.size());

      value->Set(dict);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP: {
      ListOp<value::token> lst;

      if (!ReadTokenListOp(&lst)) {
        PUSH_ERROR("Failed to read TokenListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // SdfListOp<class SdfPath>
      // => underliying storage is the array of ListOp[PathIndex]
      ListOp<Path> lst;

      if (!ReadPathListOp(&lst)) {
        PUSH_ERROR("Failed to read PathListOp data.");
        return false;
      }

      value->Set(lst);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      std::vector<Path> v;
      if (!ReadPathArray(&v)) {
        _err += "Failed to read PathVector value\n";
        return false;
      }

      DCOUT("PathVector = " << to_string(v));

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR: {

      COMPRESS_UNSUPPORTED_CHECK(dty)
      // std::vector<Index>
      uint64_t n;
      if (!_sr->read8(&n)) {
        PUSH_ERROR("Failed to read TokenVector value.");
        return false;
      }

      std::vector<crate::Index> indices(static_cast<size_t>(n));
      if (!_sr->read(static_cast<size_t>(n) * sizeof(crate::Index),
                     static_cast<size_t>(n) * sizeof(crate::Index),
                     reinterpret_cast<uint8_t *>(indices.data()))) {
        PUSH_ERROR("Failed to read TokenVector value.");
        return false;
      }

      DCOUT("TokenVector(index) = " << indices);

      std::vector<value::token> tokens(indices.size());
      for (size_t i = 0; i < indices.size(); i++) {
        tokens[i] = GetToken(indices[i]);
      }

      DCOUT("TokenVector = " << tokens);

      value->Set(tokens);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES: {

      COMPRESS_UNSUPPORTED_CHECK(dty)

      value::TimeSamples ts;
      if (!ReadTimeSamples(&ts)) {
        _err += "Failed to read TimeSamples data\n";
        return false;
      }

      value->Set(ts);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR: {

      std::vector<double> v;
      if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
        _err += "Failed to read DoubleVector value\n";
        return false;
      }

      DCOUT("DoubleArray = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR: {

      COMPRESS_UNSUPPORTED_CHECK(dty)

      std::vector<std::string> v;
      if (!ReadStringArray(&v)) {
        _err += "Failed to read StringVector value\n";
        return false;
      }

      DCOUT("StringArray = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Inlined value: " +
          crate::GetCrateDataTypeName(dty.dtype_id));
      return false;
    }
  }

#undef TODO_IMPLEMENT
#undef COMPRESS_UNSUPPORTED_CHECK
#undef NON_ARRAY_UNSUPPORTED_CHECK

  // Never should reach here.
  return false;
}

bool Parser::Impl::BuildDecompressedPathsImpl(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex, Path parentPath) {
  bool hasChild = false, hasSibling = false;
  do {
    auto thisIndex = curIndex++;
    if (parentPath.IsEmpty()) {
      // root node.
      // Assume single root node in the scene.
      DCOUT("paths[" << pathIndexes[thisIndex]
                     << "] is parent. name = " << parentPath.full_path_name());
      parentPath = Path::AbsoluteRootPath();
      _paths[pathIndexes[thisIndex]] = parentPath;
    } else {
      int32_t tokenIndex = elementTokenIndexes[thisIndex];
      bool isPrimPropertyPath = tokenIndex < 0;
      tokenIndex = std::abs(tokenIndex);

      DCOUT("tokenIndex = " << tokenIndex);
      if (tokenIndex >= int32_t(_tokens.size())) {
        PUSH_ERROR("Invalid tokenIndex in BuildDecompressedPathsImpl.");
        return false;
      }
      auto const &elemToken = _tokens[size_t(tokenIndex)];
      DCOUT("elemToken = " << elemToken);
      DCOUT("[" << pathIndexes[thisIndex] << "].append = " << elemToken);

      // full path
      _paths[pathIndexes[thisIndex]] =
          isPrimPropertyPath ? parentPath.AppendProperty(elemToken.str())
                             : parentPath.AppendElement(elemToken.str());

      // also set local path for 'primChildren' check
      _paths[pathIndexes[thisIndex]].SetLocalPart(elemToken.str());
    }

    // If we have either a child or a sibling but not both, then just
    // continue to the neighbor.  If we have both then spawn a task for the
    // sibling and do the child ourself.  We think that our path trees tend
    // to be broader more often than deep.

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        // NOTE(syoyo): This recursive call can be parallelized
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps,
                                        siblingIndex, parentPath)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent path.
      parentPath = _paths[pathIndexes[thisIndex]];
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}

// TODO(syoyo): Refactor
bool Parser::Impl::BuildNodeHierarchy(
    std::vector<uint32_t> const &pathIndexes,
    std::vector<int32_t> const &elementTokenIndexes,
    std::vector<int32_t> const &jumps, size_t curIndex,
    int64_t parentNodeIndex) {
  bool hasChild = false, hasSibling = false;

  // NOTE: Need to indirectly lookup index through pathIndexes[] when accessing
  // `_nodes`
  do {
    auto thisIndex = curIndex++;
    DCOUT("thisIndex = " << thisIndex << ", curIndex = " << curIndex);
    if (parentNodeIndex == -1) {
      // root node.
      // Assume single root node in the scene.
      assert(thisIndex == 0);

      Node root(parentNodeIndex, _paths[pathIndexes[thisIndex]]);

      _nodes[pathIndexes[thisIndex]] = root;

      parentNodeIndex = int64_t(thisIndex);

    } else {
      if (parentNodeIndex >= int64_t(_nodes.size())) {
        return false;
      }

      DCOUT("Hierarchy. parent[" << pathIndexes[size_t(parentNodeIndex)]
                                 << "].add_child = " << pathIndexes[thisIndex]);

      Node node(parentNodeIndex, _paths[pathIndexes[thisIndex]]);

      assert(_nodes[size_t(pathIndexes[thisIndex])].GetParent() == -2);

      _nodes[size_t(pathIndexes[thisIndex])] = node;

      std::string name = _paths[pathIndexes[thisIndex]].local_path_name();
      DCOUT("childName = " << name);
      _nodes[size_t(pathIndexes[size_t(parentNodeIndex)])].AddChildren(
          name, pathIndexes[thisIndex]);
    }

    hasChild = (jumps[thisIndex] > 0) || (jumps[thisIndex] == -1);
    hasSibling = (jumps[thisIndex] >= 0);

    if (hasChild) {
      if (hasSibling) {
        auto siblingIndex = thisIndex + size_t(jumps[thisIndex]);
        if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps,
                                siblingIndex, parentNodeIndex)) {
          return false;
        }
      }
      // Have a child (may have also had a sibling). Reset parent node index
      parentNodeIndex = int64_t(thisIndex);
      DCOUT("parentNodeIndex = " << parentNodeIndex);
    }
    // If we had only a sibling, we just continue since the parent path is
    // unchanged and the next thing in the reader stream is the sibling's
    // header.
  } while (hasChild || hasSibling);

  return true;
}

bool Parser::Impl::ReadCompressedPaths(const uint64_t ref_num_paths) {
  std::vector<uint32_t> pathIndexes;
  std::vector<int32_t> elementTokenIndexes;
  std::vector<int32_t> jumps;

  // Read number of encoded paths.
  uint64_t numPaths;
  if (!_sr->read8(&numPaths)) {
    _err += "Failed to read the number of paths.\n";
    return false;
  }

  if (ref_num_paths != numPaths) {
    _err += "Size mismatch of numPaths at `PATHS` section.\n";
    return false;
  }

  DCOUT("numPaths : " << numPaths);

  pathIndexes.resize(static_cast<size_t>(numPaths));
  elementTokenIndexes.resize(static_cast<size_t>(numPaths));
  jumps.resize(static_cast<size_t>(numPaths));

  // Create temporary space for decompressing.
  std::vector<char> compBuffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(numPaths)));
  std::vector<char> workingSpace(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(numPaths)));

  // pathIndexes.
  {
    uint64_t pathIndexesSize;
    if (!_sr->read8(&pathIndexesSize)) {
      _err += "Failed to read pathIndexesSize.\n";
      return false;
    }

    if (pathIndexesSize !=
        _sr->read(size_t(pathIndexesSize), size_t(pathIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      _err += "Failed to read pathIndexes data.\n";
      return false;
    }

    DCOUT("comBuffer.size = " << compBuffer.size());
    DCOUT("pathIndexesSize = " << pathIndexesSize);

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(pathIndexesSize), pathIndexes.data(),
        size_t(numPaths), &err, workingSpace.data());
    if (!err.empty()) {
      _err += "Failed to decode pathIndexes\n" + err;
      return false;
    }
  }

  // elementTokenIndexes.
  {
    uint64_t elementTokenIndexesSize;
    if (!_sr->read8(&elementTokenIndexesSize)) {
      _err += "Failed to read elementTokenIndexesSize.\n";
      return false;
    }

    if (elementTokenIndexesSize !=
        _sr->read(size_t(elementTokenIndexesSize),
                  size_t(elementTokenIndexesSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read elementTokenIndexes data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(elementTokenIndexesSize),
        elementTokenIndexes.data(), size_t(numPaths), &err,
        workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode elementTokenIndexes.");
      return false;
    }
  }

  // jumps.
  {
    uint64_t jumpsSize;
    if (!_sr->read8(&jumpsSize)) {
      PUSH_ERROR("Failed to read jumpsSize.");
      return false;
    }

    if (jumpsSize !=
        _sr->read(size_t(jumpsSize), size_t(jumpsSize),
                  reinterpret_cast<uint8_t *>(compBuffer.data()))) {
      PUSH_ERROR("Failed to read jumps data.");
      return false;
    }

    std::string err;
    Usd_IntegerCompression::DecompressFromBuffer(
        compBuffer.data(), size_t(jumpsSize), jumps.data(), size_t(numPaths),
        &err, workingSpace.data());

    if (!err.empty()) {
      PUSH_ERROR("Failed to decode jumps.");
      return false;
    }
  }

  _paths.resize(static_cast<size_t>(numPaths));

  _nodes.resize(static_cast<size_t>(numPaths));

  // Now build the paths.
  if (!BuildDecompressedPathsImpl(pathIndexes, elementTokenIndexes, jumps,
                                  /* curIndex */ 0, Path())) {
    return false;
  }

  // Now build node hierarchy.
  if (!BuildNodeHierarchy(pathIndexes, elementTokenIndexes, jumps,
                          /* curIndex */ 0, /* parent node index */ -1)) {
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i < pathIndexes.size(); i++) {
    std::cout << "pathIndexes[" << i << "] = " << pathIndexes[i] << "\n";
  }

  for (auto item : elementTokenIndexes) {
    std::cout << "elementTokenIndexes " << item << "\n";
  }

  for (auto item : jumps) {
    std::cout << "jumps " << item << "\n";
  }
#endif

  return true;
}

bool Parser::Impl::ReadSection(crate::Section *s) {
  size_t name_len = crate::kSectionNameMaxLength + 1;

  if (name_len !=
      _sr->read(name_len, name_len, reinterpret_cast<uint8_t *>(s->name))) {
    _err += "Failed to read section.name.\n";
    return false;
  }

  if (!_sr->read8(&s->start)) {
    _err += "Failed to read section.start.\n";
    return false;
  }

  if (!_sr->read8(&s->size)) {
    _err += "Failed to read section.size.\n";
    return false;
  }

  return true;
}

bool Parser::Impl::ReadTokens() {
  if ((_tokens_index < 0) || (_tokens_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `TOKENS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const crate::Section &sec = _toc.sections[size_t(_tokens_index)];
  if (!_sr->seek_set(uint64_t(sec.start))) {
    _err += "Failed to move to `TOKENS` section.\n";
    return false;
  }

  DCOUT("sec.start = " << sec.start);

  // # of tokens.
  uint64_t n;
  if (!_sr->read8(&n)) {
    _err += "Failed to read # of tokens at `TOKENS` section.\n";
    return false;
  }

  // Tokens are lz4 compressed starting from version 0.4.0

  // Compressed token data.
  uint64_t uncompressedSize;
  if (!_sr->read8(&uncompressedSize)) {
    _err += "Failed to read uncompressedSize at `TOKENS` section.\n";
    return false;
  }

  uint64_t compressedSize;
  if (!_sr->read8(&compressedSize)) {
    _err += "Failed to read compressedSize at `TOKENS` section.\n";
    return false;
  }

  DCOUT("# of tokens = " << n << ", uncompressedSize = " << uncompressedSize
                         << ", compressedSize = " << compressedSize);

  std::vector<char> chars(static_cast<size_t>(uncompressedSize));
  std::vector<char> compressed(static_cast<size_t>(compressedSize));

  if (compressedSize !=
      _sr->read(size_t(compressedSize), size_t(compressedSize),
                reinterpret_cast<uint8_t *>(compressed.data()))) {
    _err += "Failed to read compressed data at `TOKENS` section.\n";
    return false;
  }

  if (uncompressedSize !=
      LZ4Compression::DecompressFromBuffer(compressed.data(), chars.data(),
                                           size_t(compressedSize),
                                           size_t(uncompressedSize), &_err)) {
    _err += "Failed to decompress data of Tokens.\n";
    return false;
  }

  // Split null terminated string into _tokens.
  const char *ps = chars.data();
  const char *pe = chars.data() + chars.size();
  const char *p = ps;
  size_t n_remain = size_t(n);

  auto my_strnlen = [](const char *s, const size_t max_length) -> size_t {
    if (!s) return 0;

    size_t i = 0;
    for (; i < max_length; i++) {
      if (s[i] == '\0') {
        return i;
      }
    }

    // null character not found.
    return i;
  };

  // TODO(syoyo): Check if input string has exactly `n` tokens(`n` null
  // characters)
  for (size_t i = 0; i < n; i++) {
    size_t len = my_strnlen(p, n_remain);

    if ((p + len) > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    std::string str;
    if (len > 0) {
      str = std::string(p, len);
    }

    p += len + 1;  // +1 = '\0'
    n_remain = size_t(pe - p);
    assert(p <= pe);
    if (p > pe) {
      _err += "Invalid token string array.\n";
      return false;
    }

    value::token tok(str);

    DCOUT("token[" << i << "] = " << tok);
    _tokens.push_back(tok);
  }

  return true;
}

bool Parser::Impl::ReadStrings() {
  if ((_strings_index < 0) ||
      (_strings_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `STRINGS` section.\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_strings_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `STRINGS` section.\n";
    return false;
  }

  if (!ReadIndices(_sr, &_string_indices)) {
    _err += "Failed to read StringIndex array.\n";
    return false;
  }

  for (size_t i = 0; i < _string_indices.size(); i++) {
    DCOUT("StringIndex[" << i << "] = " << _string_indices[i].value);
  }

  return true;
}

bool Parser::Impl::ReadFields() {
  if ((_fields_index < 0) || (_fields_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `FIELDS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_fields_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDS` section.\n";
    return false;
  }

  uint64_t num_fields;
  if (!_sr->read8(&num_fields)) {
    _err += "Failed to read # of fields at `FIELDS` section.\n";
    return false;
  }

  _fields.resize(static_cast<size_t>(num_fields));

  // indices
  {
    std::vector<char> comp_buffer(
        Usd_IntegerCompression::GetCompressedBufferSize(
            static_cast<size_t>(num_fields)));
    // temp buffer for decompress
    std::vector<uint32_t> tmp;
    tmp.resize(static_cast<size_t>(num_fields));

    uint64_t fields_size;
    if (!_sr->read8(&fields_size)) {
      _err += "Failed to read field legnth at `FIELDS` section.\n";
      return false;
    }

    if (fields_size !=
        _sr->read(size_t(fields_size), size_t(fields_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read field data at `FIELDS` section.\n";
      return false;
    }

    std::string err;
    DCOUT("fields_size = " << fields_size << ", tmp.size = " << tmp.size()
                           << ", num_fields = " << num_fields);
    Usd_IntegerCompression::DecompressFromBuffer(
        comp_buffer.data(), size_t(fields_size), tmp.data(), size_t(num_fields),
        &err);

    if (!err.empty()) {
      _err += err;
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].token_index.value = tmp[i];
    }
  }

  // Value reps
  {
    uint64_t reps_size;
    if (!_sr->read8(&reps_size)) {
      _err += "Failed to read reps legnth at `FIELDS` section.\n";
      return false;
    }

    std::vector<char> comp_buffer(static_cast<size_t>(reps_size));

    if (reps_size !=
        _sr->read(size_t(reps_size), size_t(reps_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      _err += "Failed to read reps data at `FIELDS` section.\n";
      return false;
    }

    // reps datasize = LZ4 compressed. uncompressed size = num_fields * 8 bytes
    std::vector<uint64_t> reps_data;
    reps_data.resize(static_cast<size_t>(num_fields));

    size_t uncompressed_size = size_t(num_fields) * sizeof(uint64_t);

    if (uncompressed_size != LZ4Compression::DecompressFromBuffer(
                                 comp_buffer.data(),
                                 reinterpret_cast<char *>(reps_data.data()),
                                 size_t(reps_size), uncompressed_size, &_err)) {
      return false;
    }

    for (size_t i = 0; i < num_fields; i++) {
      _fields[i].value_rep = crate::ValueRep(reps_data[i]);
    }
  }

  DCOUT("num_fields = " << num_fields);
  for (size_t i = 0; i < num_fields; i++) {
    DCOUT("field[" << i << "] name = " << GetToken(_fields[i].token_index)
                   << ", value = " << _fields[i].value_rep.GetStringRepr());
  }

  return true;
}

bool Parser::Impl::ReadFieldSets() {
  if ((_fieldsets_index < 0) ||
      (_fieldsets_index >= int64_t(_toc.sections.size()))) {
    _err += "Invalid index for `FIELDSETS` section.\n";
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    _err += "Version must be 0.4.0 or later, but got " +
            std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
            "." + std::to_string(_version[2]) + "\n";
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_fieldsets_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    _err += "Failed to move to `FIELDSETS` section.\n";
    return false;
  }

  uint64_t num_fieldsets;
  if (!_sr->read8(&num_fieldsets)) {
    _err += "Failed to read # of fieldsets at `FIELDSETS` section.\n";
    return false;
  }

  _fieldset_indices.resize(static_cast<size_t>(num_fieldsets));

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_fieldsets)));

  std::vector<uint32_t> tmp(static_cast<size_t>(num_fieldsets));
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_fieldsets)));

  uint64_t fsets_size;
  if (!_sr->read8(&fsets_size)) {
    _err += "Failed to read fieldsets size at `FIELDSETS` section.\n";
    return false;
  }

  DCOUT("num_fieldsets = " << num_fieldsets << ", fsets_size = " << fsets_size
                           << ", comp_buffer.size = " << comp_buffer.size());

  assert(fsets_size < comp_buffer.size());

  if (fsets_size !=
      _sr->read(size_t(fsets_size), size_t(fsets_size),
                reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
    _err += "Failed to read fieldsets data at `FIELDSETS` section.\n";
    return false;
  }

  std::string err;
  Usd_IntegerCompression::DecompressFromBuffer(
      comp_buffer.data(), size_t(fsets_size), tmp.data(), size_t(num_fieldsets),
      &err, working_space.data());

  if (!err.empty()) {
    _err += err;
    return false;
  }

  for (size_t i = 0; i != num_fieldsets; ++i) {
    DCOUT("fieldset_index[" << i << "] = " << tmp[i]);
    _fieldset_indices[i].value = tmp[i];
  }

  return true;
}

bool Parser::Impl::BuildLiveFieldSets() {
  for (auto fsBegin = _fieldset_indices.begin(),
            fsEnd = std::find(fsBegin, _fieldset_indices.end(), crate::Index());
       fsBegin != _fieldset_indices.end();
       fsBegin = fsEnd + 1, fsEnd = std::find(fsBegin, _fieldset_indices.end(),
                                              crate::Index())) {
    auto &pairs = _live_fieldsets[crate::Index(
        uint32_t(fsBegin - _fieldset_indices.begin()))];

    pairs.resize(size_t(fsEnd - fsBegin));
    DCOUT("range size = " << (fsEnd - fsBegin));
    // TODO(syoyo): Parallelize.
    for (size_t i = 0; fsBegin != fsEnd; ++fsBegin, ++i) {
      if (fsBegin->value < _fields.size()) {
        // ok
      } else {
        PUSH_ERROR("Invalid live field set data.");
        return false;
      }

      DCOUT("fieldIndex = " << (fsBegin->value));
      auto const &field = _fields[fsBegin->value];
      pairs[i].first = GetToken(field.token_index).str();
      if (!UnpackValueRep(field.value_rep, &pairs[i].second)) {
        PUSH_ERROR("BuildLiveFieldSets: Failed to unpack ValueRep : "
                   << field.value_rep.GetStringRepr());
        return false;
      }
    }
  }

  DCOUT("# of live fieldsets = " << _live_fieldsets.size());

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  size_t sum = 0;
  for (const auto &item : _live_fieldsets) {
    DCOUT("livefieldsets[" << item.first.value
                           << "].count = " << item.second.size());
    sum += item.second.size();

    for (size_t i = 0; i < item.second.size(); i++) {
      DCOUT(" [" << i << "] name = " << item.second[i].first);
    }
  }
  DCOUT("Total fields used = " << sum);
#endif

  return true;
}

bool Parser::Impl::ParseAttribute(const FieldValuePairVector &fvs,
                                  PrimAttrib *attr,
                                  const std::string &prop_name) {
  bool success = false;

  DCOUT("fvs.size = " << fvs.size());

  bool has_connection{false};

  Variability variability{Variability::Varying};
  Interpolation interpolation{Interpolation::Invalid};

  // Check if required field exists.
  if (!HasFieldValuePair(fvs, kTypeName, kToken)) {
    PUSH_ERROR(
        "\"typeName\" field with `token` type must exist for Attribute data.");
    return false;
  }

  if (!HasField(kDefault)) {
    PUSH_ERROR("\"default\" field must exist for Attribute data.");
    return false;
  }

  //
  // Parse properties
  //
  for (const auto &fv : fvs) {
    DCOUT("===  fvs.first " << fv.first
                            << ", second: " << fv.second.GetTypeName());
    if ((fv.first == "typeName") && (fv.second.GetTypeName() == "Token")) {
      attr->type_name = fv.second.value<value::token>().str();
      DCOUT("typeName: " << attr->type_name);
    } else if (fv.first == "default") {
      // Nothing to do at there. Process `default` in the later
      continue;
    } else if (fv.first == "targetPaths") {
      // e.g. connection to Material.
      const ListOp<Path> paths = fv.second.value<ListOp<Path>>();

      DCOUT("ListOp<Path> = " << to_string(paths));
      // Currently we only support single explicit path.
      if ((paths.GetExplicitItems().size() == 1)) {
        const Path &path = paths.GetExplicitItems()[0];
        (void)path;

        DCOUT("full path: " << path.full_path_name());
        DCOUT("local path: " << path.local_path_name());

        attr->var.set_scalar(path.full_path_name());  // TODO: store `Path`

        has_connection = true;

      } else {
        return false;
      }
    } else if (fv.first == "connectionPaths") {
      // e.g. connection to texture file.
      const ListOp<Path> paths = fv.second.value<ListOp<Path>>();

      DCOUT("ListOp<Path> = " << to_string(paths));

      // Currently we only support single explicit path.
      if ((paths.GetExplicitItems().size() == 1)) {
        const Path &path = paths.GetExplicitItems()[0];
        (void)path;

        DCOUT("full path: " << path.full_path_name());
        DCOUT("local path: " << path.local_path_name());

        attr->var.set_scalar(path.full_path_name());  // TODO: store `Path`

        has_connection = true;

      } else {
        return false;
      }
    } else if ((fv.first == "variablity") &&
               (fv.second.GetTypeName() == "Variability")) {
      variability = fv.second.value<Variability>();
    } else if ((fv.first == "interpolation") &&
               (fv.second.GetTypeName() == "Token")) {
      interpolation =
          InterpolationFromString(fv.second.value<value::token>().str());
    } else {
      DCOUT("TODO: name: " << fv.first
                           << ", type: " << fv.second.GetTypeName());
    }
  }

  attr->variability = variability;
  attr->interpolation = interpolation;

  //
  // Decode value(stored in "default" field)
  //
  const auto fvRet = GetFieldValuePair(fvs, kDefault);
  if (!fvRet) {
    // This code path should not happen. Just in case.
    PUSH_ERROR("`default` field not found.");
    return false;
  }
  const auto fv = fvRet.value();

  auto add1DArraySuffix = [](const std::string &a) -> std::string {
    return a + "[]";
  };

  {
    if (fv.first == "default") {
      attr->name = prop_name;

      DCOUT("fv.second.GetTypeName = " << fv.second.GetTypeName());

#define PROC_SCALAR(__tyname, __ty)                   \
  }                                                   \
  else if (fv.second.GetTypeName() == __tyname) {     \
    auto ret = fv.second.get_value<__ty>();           \
    if (!ret) {                                       \
      PUSH_ERROR("Failed to decode " << __tyname << " value."); \
      return false;                                   \
    }                                                 \
    attr->var.set_scalar(ret.value());                \
    success = true;

#define PROC_ARRAY(__tyname, __ty)                       \
  }                                                      \
  else if (fv.second.GetTypeName() == add1DArraySuffix(__tyname)) {        \
    auto ret = fv.second.get_value<std::vector<__ty>>(); \
    if (!ret) {                                          \
      PUSH_ERROR("Failed to decode " << __tyname << "[] value.");  \
      return false;                                      \
    }                                                    \
    attr->var.set_scalar(ret.value());                   \
    success = true;

      if (0) {  // dummy
        PROC_SCALAR(value::kFloat, float)
        PROC_SCALAR(value::kBool, bool)
        PROC_SCALAR(value::kInt, int)
        PROC_SCALAR(value::kFloat2, value::float2)
        PROC_SCALAR(value::kFloat3, value::float3)
        PROC_SCALAR(value::kFloat4, value::float4)
        PROC_SCALAR(value::kHalf2, value::half2)
        PROC_SCALAR(value::kHalf3, value::half3)
        PROC_SCALAR(value::kHalf4, value::half4)
        PROC_SCALAR(value::kToken, value::token)
        PROC_SCALAR(value::kAssetPath, value::asset_path)

        PROC_SCALAR(value::kMatrix2d, value::matrix2d)
        PROC_SCALAR(value::kMatrix3d, value::matrix3d)
        PROC_SCALAR(value::kMatrix4d, value::matrix4d)

        // It seems `token[]` is defined as `TokenVector` in CrateData.
        // We tret it as scalar
        PROC_SCALAR("TokenVector", std::vector<value::token>)

        // TODO(syoyo): Use constexpr concat
        PROC_ARRAY(value::kInt, int32_t)
        PROC_ARRAY(value::kUInt, uint32_t)
        PROC_ARRAY(value::kFloat, float)
        PROC_ARRAY(value::kFloat2, value::float2)
        PROC_ARRAY(value::kFloat3, value::float3)
        PROC_ARRAY(value::kFloat4, value::float4)
        PROC_ARRAY(value::kToken, value::token)

        PROC_ARRAY(value::kMatrix2d, value::matrix2d)
        PROC_ARRAY(value::kMatrix3d, value::matrix3d)
        PROC_ARRAY(value::kMatrix4d, value::matrix4d)

        PROC_ARRAY(value::kPoint3h, value::point3h)
        PROC_ARRAY(value::kPoint3f, value::point3f)
        PROC_ARRAY(value::kPoint3d, value::point3d)

        PROC_ARRAY(value::kVector3h, value::vector3h)
        PROC_ARRAY(value::kVector3f, value::vector3f)
        PROC_ARRAY(value::kVector3d, value::vector3d)

        PROC_ARRAY(value::kNormal3h, value::normal3h)
        PROC_ARRAY(value::kNormal3f, value::normal3f)
        PROC_ARRAY(value::kNormal3d, value::normal3d)

        //PROC_ARRAY("Vec2fArray", value::float2)
        //PROC_ARRAY("Vec3fArray", value::float3)
        //PROC_ARRAY("Vec4fArray", value::float4)
        //PROC_ARRAY("IntArray", int)
        //PROC_ARRAY(kTokenArray, value::token)

      } else {
        PUSH_ERROR("TODO: " + fv.second.GetTypeName());
      }

    }
  }

  if (!success && has_connection) {
    // Attribute has a connection(has a path and no `default` field)
    success = true;
  }

  return success;
}

#define FIELDVALUE_DATATYPE_CHECK(__fv, __name, __req_type) { \
  if (__fv.first == __name) { \
    if (__fv.second.GetTypeName() != __req_type) { \
      PUSH_ERROR("`" << __name << "` attribute must be " << __req_type << " type, but got " << __fv.second.GetTypeName()); \
      return false; \
    } \
  } \
}

bool Parser::Impl::ReconstructXform(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Xform *xform) {
  // TODO:
  //  * [ ] !invert! suffix
  //  * [ ] !resetXformStack! suffix
  //  * [ ] maya:pivot support?

  (void)xform;

  DCOUT("Reconstruct Xform");

  for (const auto &fv : fields) {
    DCOUT("field = " << fv.first << ", type = " << fv.second.GetTypeName());

    FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

  }

  //
  // NOTE: Currently we assume one deeper node has Xform's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")");
      return false;
    }

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);

    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("Xform: prop: " << prop_name << ", ret = " << ret);
      if (ret) {
        // TODO(syoyo): Implement
        PUSH_ERROR("TODO: Implemen Xform prop: " + prop_name);
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructGeomBasisCurves(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomBasisCurves *curves) {
  bool has_position{false};

  DCOUT("Reconstruct Xform");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {

      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      auto ret = fv.second.get_value<std::vector<value::token>>();
      if (!ret) {
        PUSH_ERROR("Invalid `properties` data");
        return false;
      }

      for (size_t i = 0; i < ret.value().size(); i++) {
        if (ret.value()[i].str() == "points") {
          has_position = true;
        }
      }
    }
  }

  (void)has_position;

  //
  // NOTE: Currently we assume one deeper node has GeomMesh's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
#if 0
      _err += "GeomBasisCurves: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";
#endif
      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "points") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "got point\n";
#endif
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //   curves->points = *p;
          // }
        } else if (prop_name == "extent") {
          // vec3f[2]
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //  if (p->size() == 2) {
          //    curves->extent.value.lower = (*p)[0];
          //    curves->extent.value.upper = (*p)[1];
          //  }
          //}
        } else if (prop_name == "normals") {
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //   curves->normals = (*p);
          // }
        } else if (prop_name == "widths") {
          // if (auto p = primvar::as_vector<float>(&attr.var)) {
          //   curves->widths = (*p);
          // }
        } else if (prop_name == "curveVertexCounts") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //   curves->curveVertexCounts = (*p);
          // }
        } else if (prop_name == "type") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          // std::cout << "type:" << attr.stringVal << "\n";
#endif
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //   if (p->compare("cubic") == 0) {
          //     curves->type = "cubic";
          //   } else if (p->compare("linear") == 0) {
          //     curves->type = "linear";
          //   } else {
          //     _err += "Unknown type: " + (*p) + "\n";
          //     return false;
          //   }
          // }
        } else if (prop_name == "basis") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          // std::cout << "basis:" << attr.stringVal << "\n";
#endif
#if 0
          if (auto p = nonstd::get_if<std::string>(&attr.var)) {
            if (p->compare("bezier") == 0) {
              curves->type = "bezier";
            } else if (p->compare("catmullRom") == 0) {
              curves->type = "catmullRom";
            } else if (p->compare("bspline") == 0) {
              curves->type = "bspline";
            } else if (p->compare("hermite") == 0) {
              _err += "`hermite` basis for BasisCurves is not supported in TinyUSDZ\n";
              return false;
            } else if (p->compare("power") == 0) {
              _err += "`power` basis for BasisCurves is not supported in TinyUSDZ\n";
              return false;
            } else {
              _err += "Unknown basis: " + (*p) + "\n";
              return false;
            }
          }
#endif
        } else if (prop_name == "wrap") {
#if 0
          if (auto p = nonstd::get_if<std::string>(&attr.var)) {
            if (p->compare("nonperiodic") == 0) {
              curves->type = "nonperiodic";
            } else if (p->compare("periodic") == 0) {
              curves->type = "periodic";
            } else if (p->compare("pinned") == 0) {
              curves->type = "pinned";
            } else {
              _err += "Unknown wrap: " + (*p) + "\n";
              return false;
            }
          }
#endif
        } else {
          // Assume Primvar.
          if (curves->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          curves->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructGeomSubset(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomSubset *geom_subset) {

  DCOUT("Reconstruct Xform");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      //   // if (fv.second.GetStringArray()[i] == "points") {
      //   // }
      // }
    }
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // TODO: Should we report an error?
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "elementType") {
          auto p = attr.var.get_value<tinyusdz::value::token>();
          if (p) {
            std::string str = p->str();
            if (str == "face") {
              geom_subset->elementType = GeomSubset::ElementType::Face;
            } else {
              PUSH_ERROR("`elementType` must be `face`, but got `" + str + "`");
              return false;
            }
          } else {
            PUSH_ERROR("`elementType` must be token type, but got " +
                       value::GetTypeName(attr.var.type_id()));
            return false;
          }
        } else if (prop_name == "faces") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            geom_subset->faces = (*p);
          }

          DCOUT("faces.num = " << geom_subset->faces.size());

        } else {
          // Assume Primvar.
          if (geom_subset->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          geom_subset->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructGeomMesh(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomMesh *mesh) {
  bool has_position{false};

  DCOUT("Reconstruct Xform");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      const auto arr = fv.second.get_value<std::vector<value::token>>();
      if (!arr) {
        return false;
      }
      for (size_t i = 0; i < arr.value().size(); i++) {
        if (arr.value()[i] == "points") {
          has_position = true;
        }
      }
    }
  }

  (void)has_position;

  // Disable has_position check for a while, since Mesh may not have "points",
  // but "position"

  // if (!has_position) {
  //  _err += "No `position` field exist for Mesh node: " + node.GetLocalPath()
  //  +
  //          ".\n";
  //  return false;
  //}

  //
  // NOTE: Currently we assume one deeper node has GeomMesh's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      DCOUT("No speciefier assigned to this child node: " << child_index);
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "points") {
          auto p = attr.var.get_value<std::vector<value::point3f>>();
          if (p) {
            mesh->points = (*p);
          } else {
            PUSH_ERROR("`points` must be point3[] type, but got " +
                       value::GetTypeName(attr.var.type_id()));
            return false;
          }
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //   mesh->points = (*p);
          // }
        } else if (prop_name == "doubleSided") {
          auto p = attr.var.get_value<bool>();
          if (p) {
            mesh->doubleSided = (*p);
          }
        } else if (prop_name == "extent") {
          // vec3f[2]
          auto p = attr.var.get_value<std::vector<value::float3>>();
          if (p && p->size() == 2) {
            mesh->extent.value.lower = (*p)[0];
            mesh->extent.value.upper = (*p)[1];
          }
        } else if (prop_name == "normals") {
          mesh->normals = attr;
        } else if ((prop_name == "primvars:UVMap") &&
                   (attr.type_name == "texCoord2f[]")) {
          // Explicit UV coord attribute.
          // TODO(syoyo): Write PrimVar parser

          // Currently we only support vec2f for uv coords.
          // if (auto p = primvar::as_vector<Vec2f>(&attr.var)) {
          //  mesh->st.buffer = (*p);
          //  mesh->st.variability = attr.variability;
          //}
        } else if (prop_name == "faceVertexCounts") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            mesh->faceVertexCounts = (*p);
          }
          //}
        } else if (prop_name == "faceVertexIndices") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            mesh->faceVertexIndices = (*p);
          }

        } else if (prop_name == "holeIndices") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //     mesh->holeIndices = (*p);
          // }
        } else if (prop_name == "cornerIndices") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //     mesh->cornerIndices = (*p);
          // }
        } else if (prop_name == "cornerSharpnesses") {
          // if (auto p = primvar::as_vector<float>(&attr.var)) {
          //     mesh->cornerSharpnesses = (*p);
          // }
        } else if (prop_name == "creaseIndices") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //     mesh->creaseIndices = (*p);
          // }
        } else if (prop_name == "creaseLengths") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //   mesh->creaseLengths = (*p);
          // }
        } else if (prop_name == "creaseSharpnesses") {
          // if (auto p = primvar::as_vector<float>(&attr.var)) {
          //     mesh->creaseSharpnesses = (*p);
          // }
        } else if (prop_name == "subdivisionScheme") {
          auto p = attr.var.get_value<value::token>();
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //   if (p->compare("none") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::None;
          //   } else if (p->compare("catmullClark") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::CatmullClark;
          //   } else if (p->compare("bilinear") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::Bilinear;
          //   } else if (p->compare("loop") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::Loop;
          //   } else {
          //     _err += "Unknown subdivision scheme: " + (*p) + "\n";
          //     return false;
          //   }
          // }
        } else if (prop_name.compare("material:binding") == 0) {
          // rel
          auto p =
              attr.var.get_value<std::string>();  // rel, but treat as sting
          if (p) {
            mesh->materialBinding.materialBinding = (*p);
          }
        } else {
          // Assume Primvar.
          if (mesh->props.count(prop_name)) {
            PUSH_ERROR("Duplicated property name found: " + prop_name);
            return false;
          }

          DCOUT("add [" << prop_name << "] to generic attrs.");

          // FIXME: Look-up schema to detect if the property is `custom` or not.
          bool is_custom{false};

          mesh->props.emplace(prop_name, Property(attr, is_custom));
        }
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructMaterial(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Material *material) {
  (void)material;

  DCOUT("Parse mateiral");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Material's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
#if 0
      _err += "Material: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    (void)child_fields;
    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      if (ret) {
        if (prop_name.compare("outputs:surface") == 0) {
          auto p = attr.var.get_value<std::string>();
          if (p) {
            material->outputs_surface = (*p);
          }
        }
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "prop: " << prop_name << "\n";
#endif
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructShader(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Shader *shader) {
  (void)shader;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  //
  // Find shader type.
  //
  std::string shader_type;

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<value::token>();
          if (p) {
            shader_type = p.value().str();
          }
        }
      }
    }
  }

  if (shader_type.empty()) {
    PUSH_ERROR("`info:id` is missing in Shader.");
    return false;
  }

  return true;
}

bool Parser::Impl::ReconstructPreviewSurface(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    PreviewSurface *shader) {
  (void)shader;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Shader's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            if (p->compare("UsdPreviewSurface") != 0) {
              PUSH_ERROR("`info:id` must be `UsdPreviewSurface`.");
              return false;
            }
          }
        } else if (prop_name.compare("outputs:surface") == 0) {
          // Surface shader output available
        } else if (prop_name.compare("outputs:displacement") == 0) {
          // Displacement shader output available
        } else if (prop_name.compare("inputs:roughness") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->roughness.value = (*p);
          }
        } else if (prop_name.compare("inputs:specular") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->specular.value = (*p);
          }
        } else if (prop_name.compare("inputs:ior") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->ior.value = (*p);
          }
        } else if (prop_name.compare("inputs:opacity") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->opacity.value = (*p);
          }
        } else if (prop_name.compare("inputs:clearcoat") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->clearcoat.value = (*p);
          }
        } else if (prop_name.compare("inputs:clearcoatRoughness") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->clearcoatRoughness.value = (*p);
          }
        } else if (prop_name.compare("inputs:metallic") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->metallic.value = (*p);
          }
        } else if (prop_name.compare("inputs:metallic.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          auto p = attr.var.get_value<std::string>();
          if (p) {
            shader->metallic.path = *p;
          }
        } else if (prop_name.compare("inputs:diffuseColor") == 0) {
          auto p = attr.var.get_value<value::float3>();
          if (p) {
            shader->diffuseColor.color = (*p);

            DCOUT("diffuseColor: " << shader->diffuseColor.color[0] << ", "
                                   << shader->diffuseColor.color[1] << ", "
                                   << shader->diffuseColor.color[2]);
          }
        } else if (prop_name.compare("inputs:diffuseColor.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          auto p = attr.var.get_value<std::string>();
          if (p) {
            shader->diffuseColor.path = *p;
          }
        } else if (prop_name.compare("inputs:emissiveColor") == 0) {
          // if (auto p = primvar::as_basic<value::float3>(&attr.var)) {
          //  shader->emissiveColor.color = (*p);

          //}
        } else if (prop_name.compare("inputs:emissiveColor.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //  shader->emissiveColor.path = *p;
          //}
        }
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructSkelRoot(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    SkelRoot *skelRoot) {
  DCOUT("Parse skelRoot");
  (void)skelRoot;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ").");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop:" << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            // shader_type = (*p);
          }
        }
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructSkeleton(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Skeleton *skeleton) {
  DCOUT("Parse skeleton");
  (void)skeleton;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop:" << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            // shader_type = (*p);
          }
        }
      }
    }
  }

  return true;
}

bool Parser::Impl::ReconstructSceneRecursively(
    int parent, int level,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Scene *scene) {

  DCOUT("ReconstructSceneRecursively: parent = " << std::to_string(parent) << ", level = " << std::to_string(level));

  if ((parent < 0) || (parent >= int(_nodes.size()))) {
    PUSH_ERROR("Invalid parent node id: " + std::to_string(parent) +
               ". Must be in range [0, " + std::to_string(_nodes.size()) + ")");
    return false;
  }

  const Node &node = _nodes[size_t(parent)];

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  auto IndentStr = [](int l) -> std::string {
    std::string indent;
    for (size_t i = 0; i < size_t(l); i++) {
      indent += "  ";
    }

    return indent;
  };
  std::cout << IndentStr(level) << "lv[" << level << "] node_index[" << parent
            << "] " << node.GetLocalPath() << " ==\n";
  std::cout << IndentStr(level) << " childs = [";
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    std::cout << node.GetChildren()[i];
    if (i != (node.GetChildren().size() - 1)) {
      std::cout << ", ";
    }
  }
  std::cout << "]\n";
#endif

  if (!path_index_to_spec_index_map.count(uint32_t(parent))) {
    // No specifier assigned to this node.
    DCOUT("No specifier assigned to this node: " << parent);
    return true;  // would be OK.
  }

  uint32_t spec_index = path_index_to_spec_index_map.at(uint32_t(parent));
  if (spec_index >= _specs.size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
    return false;
  }

  const crate::Spec &spec = _specs[spec_index];

  DCOUT(Indent(uint32_t(level)) << "  specTy = " << to_string(spec.spec_type));
  DCOUT(Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if (!_live_fieldsets.count(spec.fieldset_index)) {
    PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
               " must exist in live fieldsets.");
    return false;
  }

  const FieldValuePairVector &fields = _live_fieldsets.at(spec.fieldset_index);

  // root only attributes.
  if (parent == 0) {
    for (const auto &fv : fields) {
      if (fv.first == "upAxis") {
        auto vt = fv.second.get_value<value::token>();
        if (!vt) {
          PUSH_ERROR("`upAxis` must be `token` type.");
          return false;
        }

        std::string v = vt.value().str();
        if ((v != "Y") && (v != "Z") && (v != "X")) {
          PUSH_ERROR("`upAxis` must be 'X', 'Y' or 'Z' but got '" + v + "'");
          return false;
        }
        DCOUT("upAxis = " << v);
        scene->upAxis = std::move(v);

      } else if (fv.first == "metersPerUnit") {
        if (auto vf = fv.second.get_value<float>()) {
          scene->metersPerUnit = double(vf.value());
        } else if (auto vd = fv.second.get_value<double>()) {
          scene->metersPerUnit = vd.value();
        } else {
          PUSH_ERROR(
              "`metersPerUnit` value must be double or float type, but got '" +
              fv.second.GetTypeName() + "'");
          return false;
        }
        DCOUT("metersPerUnit = " << scene->metersPerUnit);
      } else if (fv.first == "timeCodesPerSecond") {
        if (auto vf = fv.second.get_value<float>()) {
          scene->timeCodesPerSecond = double(vf.value());
        } else if (auto vd = fv.second.get_value<double>()) {
          scene->timeCodesPerSecond = vd.value();
        } else {
          PUSH_ERROR(
              "`timeCodesPerSecond` value must be double or float "
              "type, but got '" +
              fv.second.GetTypeName() + "'");
          return false;
        }
        DCOUT("timeCodesPerSecond = " << scene->timeCodesPerSecond);
      } else if ((fv.first == "defaultPrim")) {
        auto v = fv.second.get_value<value::token>();
        if (!v) {
          PUSH_ERROR("`defaultPrim` must be `token` type.");
          return false;
        }

        scene->defaultPrim = v.value().str();
        DCOUT("defaultPrim = " << scene->defaultPrim);
      } else if (fv.first == "customLayerData") {
        if (auto v = fv.second.get_value<crate::CrateValue::Dictionary>()) {
          auto dict = v.value();
          (void)dict;
          PUSH_WARN("TODO: Store customLayerData.");
          // scene->customLayerData = fv.second.GetDictionary();
        } else {
          PUSH_ERROR("customLayerData must be `dict` type.");
          return false;
        }
      } else if (fv.first == "primChildren") {
        auto v = fv.second.get_value<std::vector<value::token>>();
        if (!v) {
          PUSH_ERROR("Type must be TokenArray for `primChildren`, but got " +
                     fv.second.GetTypeName() + "\n");
          return false;
        }

        // convert to string.
        std::vector<std::string> children;
        for (const auto &item : v.value()) {
          children.push_back(item.str());
        }
        scene->primChildren = children;
        DCOUT("primChildren = " << children);
      } else if (fv.first == "documentation") {  // 'doc'

        auto v = fv.second.get_value<std::string>();
        if (!v) {
          PUSH_ERROR("Type must be String for `documentation`, but got " +
                     fv.second.GetTypeName() + "\n");
          return false;
        }
        scene->doc = v.value();
        DCOUT("doc = " << v.value());
      } else {
        PUSH_WARN("TODO: " + fv.first + "\n");
        //_err += "TODO: " + fv.first + "\n";
        return false;
        // TODO(syoyo):
      }
    }
  }

  std::string node_type;
  crate::CrateValue::Dictionary assetInfo;

  auto GetTypeName = [](const FieldValuePairVector &fvs) -> nonstd::optional<std::string> {
    for (const auto &fv : fvs) {
      DCOUT(" fv.first = " << fv.first << ", type = " << fv.second.GetTypeName());
      if (fv.first == "typeName") {
        auto v = fv.second.get_value<value::token>();
        if (v) {
          return v.value().str();
        }
      }
    }

    return nonstd::nullopt;
  };

  DCOUT("---");

  if (auto v = GetTypeName(fields)) {
    node_type = v.value();
  }

  DCOUT("===");

#if 0  // TODO: Refactor)
  for (const auto &fv : fields) {
    DCOUT(IndentStr(level) << "  \"" << fv.first
                           << "\" : ty = " << fv.second.GetTypeName());

    if (fv.second.GetTypeId() == VALUE_TYPE_SPECIFIER) {
      DCOUT(IndentStr(level)
            << "    specifier = " << to_string(fv.second.GetSpecifier()));
    } else if ((fv.first == "primChildren") &&
               (fv.second.GetTypeName() == "TokenArray")) {
      // Check if TokenArray contains known child nodes
      const auto &tokens = fv.second.GetStringArray();

      // bool valid = true;
      for (const auto &token : tokens) {
        if (!node.GetPrimChildren().count(token)) {
          _err += "primChild '" + token + "' not found in node '" +
                  node.GetPath().full_path_name() + "'\n";
          // valid = false;
          break;
        }
      }
    } else if (fv.second.GetTypeName() == "TokenArray") {
      assert(fv.second.IsArray());
      const auto &strs = fv.second.GetStringArray();
      for (const auto &str : strs) {
        (void)str;
        DCOUT(IndentStr(level + 2) << str);
      }
    } else if ((fv.first == "customLayerData") &&
               (fv.second.GetTypeName() == "Dictionary")) {
      const auto &dict = fv.second.GetDictionary();

      for (const auto &item : dict) {
        if (item.second.GetTypeName() == "String") {
          scene->customLayerData[item.first] = item.second.GetString();
        } else if (item.second.GetTypeName() == "IntArray") {
          const auto arr = item.second.GetIntArray();
          scene->customLayerData[item.first] = arr;
        } else {
          PUSH_WARN("TODO(customLayerData): name " + item.first + ", type " +
                    item.second.GetTypeName());
        }
      }

    } else if (fv.second.GetTypeName() == "TokenListOp") {
      PUSH_WARN("TODO: name " + fv.first + ", type TokenListOp.");
    } else if (fv.second.GetTypeName() == "Vec3fArray") {
      PUSH_WARN("TODO: name: " + fv.first +
                ", type: " + fv.second.GetTypeName());

    } else if ((fv.first == "assetInfo") &&
               (fv.second.GetTypeName() == "Dictionary")) {
      node_type = "assetInfo";
      assetInfo = fv.second.GetDictionary();

    } else {
      PUSH_WARN("TODO: name: " + fv.first +
                ", type: " + fv.second.GetTypeName());
      // return false;
    }
  }
#endif

  DCOUT("node_type = " << node_type);


  if (node_type == "Xform") {
    Xform xform;
    if (!ReconstructXform(node, fields, path_index_to_spec_index_map, &xform)) {
      _err += "Failed to reconstruct Xform.\n";
      return false;
    }
    scene->xforms.push_back(xform);
  } else if (node_type == "BasisCurves") {
    GeomBasisCurves curves;
    if (!ReconstructGeomBasisCurves(node, fields, path_index_to_spec_index_map,
                                    &curves)) {
      _err += "Failed to reconstruct GeomBasisCurves.\n";
      return false;
    }
    curves.name = node.GetLocalPath();  // FIXME
    scene->geom_basis_curves.push_back(curves);
  } else if (node_type == "GeomSubset") {
    GeomSubset geom_subset;
    // TODO(syoyo): Pass Parent 'Geom' node.
    if (!ReconstructGeomSubset(node, fields, path_index_to_spec_index_map,
                               &geom_subset)) {
      _err += "Failed to reconstruct GeomSubset.\n";
      return false;
    }
    geom_subset.name = node.GetLocalPath();  // FIXME
    // TODO(syoyo): add GeomSubset to parent `Mesh`.
    _err += "TODO: Add GeomSubset to Mesh.\n";
    return false;

  } else if (node_type == "Mesh") {
    GeomMesh mesh;
    if (!ReconstructGeomMesh(node, fields, path_index_to_spec_index_map,
                             &mesh)) {
      PUSH_ERROR("Failed to reconstruct GeomMesh.");
      return false;
    }
    mesh.name = node.GetLocalPath();  // FIXME
    scene->geom_meshes.push_back(mesh);
  } else if (node_type == "Material") {
    Material material;
    if (!ReconstructMaterial(node, fields, path_index_to_spec_index_map,
                             &material)) {
      PUSH_ERROR("Failed to reconstruct Material.");
      return false;
    }
    material.name = node.GetLocalPath();  // FIXME
    scene->materials.push_back(material);
  } else if (node_type == "Shader") {
    Shader shader;
    if (!ReconstructShader(node, fields, path_index_to_spec_index_map,
                           &shader)) {
      PUSH_ERROR("Failed to reconstruct PreviewSurface(Shader).");
      return false;
    }

    shader.name = node.GetLocalPath();  // FIXME

    scene->shaders.push_back(shader);
  } else if (node_type == "Scope") {
    std::cout << "TODO: Scope\n";
  } else if (node_type == "assetInfo") {
    PUSH_WARN("TODO: Reconstruct dictionary value of `assetInfo`");
    //_nodes[size_t(parent)].SetAssetInfo(assetInfo);
  } else if (node_type == "Skeleton") {
    Skeleton skeleton;
    if (!ReconstructSkeleton(node, fields, path_index_to_spec_index_map,
                             &skeleton)) {
      PUSH_ERROR("Failed to reconstruct Skeleton.");
      return false;
    }

    skeleton.name = node.GetLocalPath();  // FIXME

    scene->skeletons.push_back(skeleton);

  } else if (node_type == "SkelRoot") {
    SkelRoot skelRoot;
    if (!ReconstructSkelRoot(node, fields, path_index_to_spec_index_map,
                             &skelRoot)) {
      PUSH_ERROR("Failed to reconstruct SkelRoot.");
      return false;
    }

    skelRoot.name = node.GetLocalPath();  // FIXME

    scene->skel_roots.push_back(skelRoot);
  } else {
    if (!node_type.empty()) {
      DCOUT("TODO or we can ignore this node: node_type: " << node_type);
    }
  }

  DCOUT("Children.size = " << node.GetChildren().size());

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    if (!ReconstructSceneRecursively(int(node.GetChildren()[i]), level + 1,
                                     path_index_to_spec_index_map, scene)) {
      return false;
    }
  }

  return true;
}

bool Parser::Impl::ReconstructScene(Scene *scene) {
  if (_nodes.empty()) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  std::unordered_map<uint32_t, uint32_t>
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs.size(); i++) {
      if (_specs[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      assert(path_index_to_spec_index_map.count(_specs[i].path_index.value) ==
             0);
      path_index_to_spec_index_map[_specs[i].path_index.value] = uint32_t(i);
    }
  }

  int root_node_id = 0;

  bool ret = ReconstructSceneRecursively(root_node_id, /* level */ 0,
                                         path_index_to_spec_index_map, scene);

  if (!ret) {
    _err += "Failed to reconstruct scene.\n";
    return false;
  }

  return true;
}

bool Parser::Impl::ReadSpecs() {
  if ((_specs_index < 0) || (_specs_index >= int64_t(_toc.sections.size()))) {
    PUSH_ERROR("Invalid index for `SPECS` section.");
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    PUSH_ERROR("Version must be 0.4.0 or later, but got " +
               std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
               "." + std::to_string(_version[2]));
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_specs_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    PUSH_ERROR("Failed to move to `SPECS` section.");
    return false;
  }

  uint64_t num_specs;
  if (!_sr->read8(&num_specs)) {
    PUSH_ERROR("Failed to read # of specs size at `SPECS` section.");
    return false;
  }

  DCOUT("num_specs " << num_specs);

  _specs.resize(static_cast<size_t>(num_specs));

  // Create temporary space for decompressing.
  std::vector<char> comp_buffer(Usd_IntegerCompression::GetCompressedBufferSize(
      static_cast<size_t>(num_specs)));

  std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));
  std::vector<char> working_space(
      Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(
          static_cast<size_t>(num_specs)));

  // path indices
  {
    uint64_t path_indexes_size;
    if (!_sr->read8(&path_indexes_size)) {
      PUSH_ERROR("Failed to read path indexes size at `SPECS` section.");
      return false;
    }

    assert(path_indexes_size < comp_buffer.size());

    if (path_indexes_size !=
        _sr->read(size_t(path_indexes_size), size_t(path_indexes_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR("Failed to read path indexes data at `SPECS` section.");
      return false;
    }

    std::string err;  // not used
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(path_indexes_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      PUSH_ERROR("Failed to decode pathIndexes at `SPECS` section.");
      return false;
    }

    for (size_t i = 0; i < num_specs; ++i) {
      DCOUT("spec[" << i << "].path_index = " << tmp[i]);
      _specs[i].path_index.value = tmp[i];
    }
  }

  // fieldset indices
  {
    uint64_t fset_indexes_size;
    if (!_sr->read8(&fset_indexes_size)) {
      PUSH_ERROR("Failed to read fieldset indexes size at `SPECS` section.");
      return false;
    }

    assert(fset_indexes_size < comp_buffer.size());

    if (fset_indexes_size !=
        _sr->read(size_t(fset_indexes_size), size_t(fset_indexes_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR("Failed to read fieldset indexes data at `SPECS` section.");
      return false;
    }

    std::string err;  // not used
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(fset_indexes_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      PUSH_ERROR("Failed to decode fieldset indices at `SPECS` section.");
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      DCOUT("specs[" << i << "].fieldset_index = " << tmp[i]);
      _specs[i].fieldset_index.value = tmp[i];
    }
  }

  // spec types
  {
    uint64_t spectype_size;
    if (!_sr->read8(&spectype_size)) {
      PUSH_ERROR("Failed to read spectype size at `SPECS` section.");
      return false;
    }

    assert(spectype_size < comp_buffer.size());

    if (spectype_size !=
        _sr->read(size_t(spectype_size), size_t(spectype_size),
                  reinterpret_cast<uint8_t *>(comp_buffer.data()))) {
      PUSH_ERROR("Failed to read spectype data at `SPECS` section.");
      return false;
    }

    std::string err;  // not used.
    if (!Usd_IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), size_t(spectype_size), tmp.data(),
            size_t(num_specs), &err, working_space.data())) {
      PUSH_ERROR("Failed to decode fieldset indices at `SPECS` section.\n");
      return false;
    }

    for (size_t i = 0; i != num_specs; ++i) {
      // std::cout << "spectype = " << tmp[i] << "\n";
      _specs[i].spec_type = static_cast<SpecType>(tmp[i]);
    }
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  for (size_t i = 0; i != num_specs; ++i) {
    DCOUT("spec[" << i << "].pathIndex  = " << _specs[i].path_index.value
                  << ", fieldset_index = " << _specs[i].fieldset_index.value
                  << ", spec_type = "
                  << tinyusdz::to_string(_specs[i].spec_type));
    DCOUT("spec[" << i << "] string_repr = "
                  << GetSpecString(crate::Index(uint32_t(i))));
  }
#endif

  return true;
}

bool Parser::Impl::ReadPaths() {
  if ((_paths_index < 0) || (_paths_index >= int64_t(_toc.sections.size()))) {
    PUSH_ERROR("Invalid index for `PATHS` section.");
    return false;
  }

  if ((_version[0] == 0) && (_version[1] < 4)) {
    PUSH_ERROR("Version must be 0.4.0 or later, but got " +
               std::to_string(_version[0]) + "." + std::to_string(_version[1]) +
               "." + std::to_string(_version[2]));
    return false;
  }

  const crate::Section &s = _toc.sections[size_t(_paths_index)];

  if (!_sr->seek_set(uint64_t(s.start))) {
    PUSH_ERROR("Failed to move to `PATHS` section.");
    return false;
  }

  uint64_t num_paths;
  if (!_sr->read8(&num_paths)) {
    PUSH_ERROR("Failed to read # of paths at `PATHS` section.");
    return false;
  }

  if (!ReadCompressedPaths(num_paths)) {
    PUSH_ERROR("Failed to read compressed paths.");
    return false;
  }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "# of paths " << _paths.size() << "\n";

  for (size_t i = 0; i < _paths.size(); i++) {
    std::cout << "path[" << i << "] = " << _paths[i].full_path_name() << "\n";
  }
#endif

  return true;
}

bool Parser::Impl::ReadBootStrap() {
  // parse header.
  uint8_t magic[8];
  if (8 != _sr->read(/* req */ 8, /* dst len */ 8, magic)) {
    PUSH_ERROR("Failed to read magic number.");
    return false;
  }

  if (memcmp(magic, "PXR-USDC", 8)) {
    PUSH_ERROR("Invalid magic number. Expected 'PXR-USDC' but got '" +
               std::string(magic, magic + 8) + "'");
    return false;
  }

  // parse version(first 3 bytes from 8 bytes)
  uint8_t version[8];
  if (8 != _sr->read(8, 8, version)) {
    PUSH_ERROR("Failed to read magic number.");
    return false;
  }

  DCOUT("version = " << int(version[0]) << "." << int(version[1]) << "."
                     << int(version[2]));

  _version[0] = version[0];
  _version[1] = version[1];
  _version[2] = version[2];

  // We only support version 0.4.0 or later.
  if ((version[0] == 0) && (version[1] < 4)) {
    PUSH_ERROR("Version must be 0.4.0 or later, but got " +
               std::to_string(version[0]) + "." + std::to_string(version[1]) +
               "." + std::to_string(version[2]));
    return false;
  }

  _toc_offset = 0;
  if (!_sr->read8(&_toc_offset)) {
    PUSH_ERROR("Failed to read TOC offset.");
    return false;
  }

  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    PUSH_ERROR("Invalid TOC offset value: " + std::to_string(_toc_offset) +
               ", filesize = " + std::to_string(_sr->size()) + ".");
    return false;
  }

  DCOUT("toc offset = " << _toc_offset);

  return true;
}

bool Parser::Impl::ReadTOC() {
  if ((_toc_offset <= 88) || (_toc_offset >= int64_t(_sr->size()))) {
    PUSH_ERROR("Invalid toc offset.");
    return false;
  }

  if (!_sr->seek_set(uint64_t(_toc_offset))) {
    PUSH_ERROR("Failed to move to TOC offset.");
    return false;
  }

  // read # of sections.
  uint64_t num_sections{0};
  if (!_sr->read8(&num_sections)) {
    PUSH_ERROR("Failed to read TOC(# of sections).");
    return false;
  }

  DCOUT("toc sections = " << num_sections);

  _toc.sections.resize(static_cast<size_t>(num_sections));

  for (size_t i = 0; i < num_sections; i++) {
    if (!ReadSection(&_toc.sections[i])) {
      PUSH_ERROR("Failed to read TOC section at " + std::to_string(i));
      return false;
    }
    DCOUT("section[" << i << "] name = " << _toc.sections[i].name
                     << ", start = " << _toc.sections[i].start
                     << ", size = " << _toc.sections[i].size);

    // find index
    if (0 == strncmp(_toc.sections[i].name, "TOKENS",
                     crate::kSectionNameMaxLength)) {
      _tokens_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "STRINGS",
                            crate::kSectionNameMaxLength)) {
      _strings_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDS",
                            crate::kSectionNameMaxLength)) {
      _fields_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "FIELDSETS",
                            crate::kSectionNameMaxLength)) {
      _fieldsets_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "SPECS",
                            crate::kSectionNameMaxLength)) {
      _specs_index = int64_t(i);
    } else if (0 == strncmp(_toc.sections[i].name, "PATHS",
                            crate::kSectionNameMaxLength)) {
      _paths_index = int64_t(i);
    }
  }

  return true;
}

//
// -- Interface --
//
Parser::Parser(StreamReader *sr, int num_threads) {
  impl_ = new Parser::Impl(sr, num_threads);
}

Parser::~Parser() {
  delete impl_;
  impl_ = nullptr;
}

bool Parser::ReadTOC() { return impl_->ReadTOC(); }

bool Parser::ReadBootStrap() { return impl_->ReadBootStrap(); }

bool Parser::ReadTokens() { return impl_->ReadTokens(); }

bool Parser::ReadStrings() { return impl_->ReadStrings(); }

bool Parser::ReadFields() { return impl_->ReadFields(); }

bool Parser::ReadFieldSets() { return impl_->ReadFieldSets(); }

bool Parser::ReadPaths() { return impl_->ReadPaths(); }

bool Parser::ReadSpecs() { return impl_->ReadSpecs(); }

bool Parser::BuildLiveFieldSets() { return impl_->BuildLiveFieldSets(); }

bool Parser::ReconstructScene(Scene *scene) {
  return impl_->ReconstructScene(scene);
}

std::string Parser::GetError() { return impl_->GetError(); }

std::string Parser::GetWarning() { return impl_->GetWarning(); }

size_t Parser::NumPaths() { return impl_->NumPaths(); }

Path Parser::GetPath(crate::Index index) { return impl_->GetPath(index); }

}  // namespace usdc
}  // namespace tinyusdz
