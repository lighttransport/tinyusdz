// SPDX-License-Identifier: Apache 2.0
// Copyright 2020 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// USDC(Crate) reader
//
// TODO:
//
// - [ ] Validate the existence of connection Paths(Connection) and target
// Paths(Relation)
// - [ ] GeomSubset
// - [ ] Refactor Variant related code.
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "usdc-reader.hh"
#include "parser-timing.hh"
#include "enum-handlers.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_READER)

#include <stack>
#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"
#include "layer.hh"
#include "tinyusdz.hh"
#include "value-types.hh"
#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"
#include "crate-pprint.hh"
#include "crate-reader.hh"
#include "mmap-array-ref.hh"
#include "tiny-container.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "path-util.hh"
#include "pprinter.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tiny-format.hh"
#include "value-pprint.hh"
#include "usdShade.hh"
#include "ascii-parser.hh"

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
#include "common-macros.inc"

namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty)                                      \
  template <>                                                            \
  bool ReconstructPrim<__ty>(const Specifier &spec, PropertyMap &, const ReferenceList &, \
                             __ty *, std::string *, std::string *, const PrimReconstructOptions &)

RECONSTRUCT_PRIM_DECL(Xform);
RECONSTRUCT_PRIM_DECL(Model);
RECONSTRUCT_PRIM_DECL(Scope);
RECONSTRUCT_PRIM_DECL(GeomPoints);
RECONSTRUCT_PRIM_DECL(GeomMesh);
RECONSTRUCT_PRIM_DECL(GeomCapsule);
RECONSTRUCT_PRIM_DECL(GeomCube);
RECONSTRUCT_PRIM_DECL(GeomCone);
RECONSTRUCT_PRIM_DECL(GeomCylinder);
RECONSTRUCT_PRIM_DECL(GeomSphere);
RECONSTRUCT_PRIM_DECL(GeomSubset);
RECONSTRUCT_PRIM_DECL(GeomBasisCurves);
RECONSTRUCT_PRIM_DECL(GeomNurbsCurves);
RECONSTRUCT_PRIM_DECL(GeomCamera);
RECONSTRUCT_PRIM_DECL(GeomPointInstancer);
RECONSTRUCT_PRIM_DECL(SphereLight);
RECONSTRUCT_PRIM_DECL(DomeLight);
RECONSTRUCT_PRIM_DECL(DiskLight);
RECONSTRUCT_PRIM_DECL(DistantLight);
RECONSTRUCT_PRIM_DECL(CylinderLight);
RECONSTRUCT_PRIM_DECL(RectLight);
RECONSTRUCT_PRIM_DECL(GeometryLight);
RECONSTRUCT_PRIM_DECL(SkelRoot);
RECONSTRUCT_PRIM_DECL(SkelAnimation);
RECONSTRUCT_PRIM_DECL(Skeleton);
RECONSTRUCT_PRIM_DECL(BlendShape);
RECONSTRUCT_PRIM_DECL(Material);
RECONSTRUCT_PRIM_DECL(Shader);
RECONSTRUCT_PRIM_DECL(NodeGraph);

#undef RECONSTRUCT_PRIM_DECL

}  // namespace prim

namespace usdc {

constexpr auto kTag = "[USDC]";

// TODO: Unify with ascii-parser.cc
static void RegisterPrimAttrTypes(std::set<std::string> &d) {
  d.clear();

  d.insert(value::kBool);

  d.insert(value::kInt64);

  d.insert(value::kInt);
  d.insert(value::kInt2);
  d.insert(value::kInt3);
  d.insert(value::kInt4);

  d.insert(value::kUInt64);

  d.insert(value::kUInt);
  d.insert(value::kUInt2);
  d.insert(value::kUInt3);
  d.insert(value::kUInt4);

  d.insert(value::kFloat);
  d.insert(value::kFloat2);
  d.insert(value::kFloat3);
  d.insert(value::kFloat4);

  d.insert(value::kDouble);
  d.insert(value::kDouble2);
  d.insert(value::kDouble3);
  d.insert(value::kDouble4);

  d.insert(value::kHalf);
  d.insert(value::kHalf2);
  d.insert(value::kHalf3);
  d.insert(value::kHalf4);

  d.insert(value::kQuath);
  d.insert(value::kQuatf);
  d.insert(value::kQuatd);

  d.insert(value::kNormal3f);
  d.insert(value::kPoint3f);
  d.insert(value::kTexCoord2h);
  d.insert(value::kTexCoord3h);
  d.insert(value::kTexCoord4h);
  d.insert(value::kTexCoord2f);
  d.insert(value::kTexCoord3f);
  d.insert(value::kTexCoord4f);
  d.insert(value::kTexCoord2d);
  d.insert(value::kTexCoord3d);
  d.insert(value::kTexCoord4d);
  d.insert(value::kVector3f);
  d.insert(value::kVector4f);
  d.insert(value::kColor3h);
  d.insert(value::kColor3f);
  d.insert(value::kColor3d);
  d.insert(value::kColor4h);
  d.insert(value::kColor4f);
  d.insert(value::kColor4d);

  // Allow `matrixNf` type for USDC
  d.insert(value::kMatrix2f);
  d.insert(value::kMatrix3f);
  d.insert(value::kMatrix4f);

  d.insert(value::kMatrix2d);
  d.insert(value::kMatrix3d);
  d.insert(value::kMatrix4d);

  d.insert(value::kToken);
  d.insert(value::kString);

  d.insert(value::kRelationship);
  d.insert(value::kAssetPath);

  d.insert(value::kDictionary);

  // variantSet. Require special treatment.
  // d.insert("variantSet");

  // TODO: Add more types...
}


static bool IsUnregisteredValueType(const std::string &typeName)
{
  std::string tyname = typeName;

  //bool array_type = false;
  if (endsWith(typeName, "[]")) {
    tyname = removeSuffix(typeName, "[]");
    //array_type = true;
  }

  // TODO: Define in crate-format?
  if (tyname == value::TypeTraits<value::uint2>::type_name()) {
    return true;
  }
  if (tyname == value::TypeTraits<value::uint3>::type_name()) {
    return true;
  }
  if (tyname == value::TypeTraits<value::uint4>::type_name()) {
    return true;
  }

  return false;

}

class USDCReader::Impl {
 public:
  Impl(StreamReader *sr, const USDCReaderConfig &config) : _sr(sr) {
    set_reader_config(config);
    RegisterPrimAttrTypes(_supported_prim_attr_types);
  }

  ~Impl() {
    delete crate_reader;
    crate_reader = nullptr;
  }

  void set_reader_config(const USDCReaderConfig &config) {
    _config = config;

#if defined(__wasi__)
    _config.numThreads = 1;
#else
    if (_config.numThreads == -1) {
      _config.numThreads =
          (std::max)(1, int(std::thread::hardware_concurrency()));
    }
    // Limit to 1024 threads.
    _config.numThreads = (std::min)(1024, _config.numThreads);
#endif
  }

  const USDCReaderConfig get_reader_config() const {
    return _config;
  }
  
  void set_progress_callback(usdc::ProgressCallback callback, void *userptr) {
    _progress_callback = callback;
    _progress_userptr = userptr;
  }

  bool ReadUSDC();

  using PathIndexToSpecIndexMap = std::unordered_map<uint32_t, uint32_t>;

  ///
  /// Construct Property(Attribute, Relationship/Connection) from
  /// FieldValuePairs
  ///
  // allow_move_from_fvs: When true, ParseProperty may move large values
  // out of fvs (used in lazy mode where fvs is a local scratch buffer).
  bool ParseProperty(const SpecType specType,
                     crate::FieldValuePairVector &fvs, Property *prop,
                     bool allow_move_from_fvs = false);

  ///
  /// Parse Prim spec from FieldValuePairs
  ///
  bool ParsePrimSpec(const crate::FieldValuePairVector &fvs,
                     nonstd::optional<std::string> &typeName, /* out */
                     nonstd::optional<Specifier> &specifier,  /* out */
                     std::vector<value::token> &primChildren,   /* out */
                     std::vector<value::token> &properties,   /* out */
                     PrimMeta &primMeta);                     /* out */

  bool ParseVariantSetFields(
      const crate::FieldValuePairVector &fvs,
      std::vector<value::token> &variantChildren); /* out */

  template <typename T>
  bool ReconstructPrim(const Specifier &spec, const crate::CrateReader::Node &node,
                       const PathIndexToSpecIndexMap &psmap, T *prim);

  ///
  /// Reconstrcut Prim node.
  /// Returns the pointer of reconstructed Prim to `primOut`
  /// When `current` is 0(StageMeta), `primOut` is not set.
  /// `is_parent_variant` : True when parent path is Variant
  /// Uses unique_ptr for move-friendly output (no Prim copies)
  ///
  bool ReconstructPrimNode(int parent, int current, int level,
                           bool is_parent_variant,
                           const PathIndexToSpecIndexMap &psmap, Stage *stage,
                           std::unique_ptr<Prim> *primOut);

  ///
  /// Reconstrcut PrimSpec node.
  /// Returns reconstruct PrimSpec to `primOut`
  /// When `current` is 0(StageMeta), `primOut` is not set.
  /// `is_parent_variant` : True when parent path is Variant
  ///
  /// TODO: Unify code with ReconstructPrimNode.
  ///
  bool ReconstructPrimSpecNode(int parent, int current, int level,
                           bool is_parent_variant,
                           const PathIndexToSpecIndexMap &psmap, Layer *layer,
                           PrimSpec *primOut);

  ///
  /// Reconstruct Prim from given `typeName` string(e.g. "Xform")
  ///
  /// @param[out] is_unsupported_prim true when encounter Unsupported Prim type(and returns nullptr)
  /// @return unique_ptr to reconstructed Prim, or nullptr on failure
  ///
  std::unique_ptr<Prim> ReconstructPrimFromTypeName(
      const std::string &typeName, const std::string &primTypeName, const std::string &prim_name,
      const crate::CrateReader::Node &node, const Specifier spec,
      const std::vector<value::token> &primChildren,
      const std::vector<value::token> &properties,
      const PathIndexToSpecIndexMap &psmap, const PrimMeta &meta, bool *is_unsupported_prim = nullptr);

  bool ReconstructPrimRecursively(int parent_id, int current_id, Prim *rootPrimPtr,
                                  int level,
                                  const PathIndexToSpecIndexMap &psmap,
                                  Stage *stage);

  //bool ReconstructPrimTree(Prim *rootPrim, const PathIndexToSpecIndexMap &psmap,
  //                         Stage *stage);

  bool ReconstructStage(Stage *stage);

  ///
  /// For Layer
  ///

  bool ReconstructPrimSpecRecursively(int parent_id, int current_id, PrimSpec *rootPrim,
                                  int level,
                                  const PathIndexToSpecIndexMap &psmap,
                                  Layer *stage);

  //bool ReconstructPrimSpecTree(PrimSpec *rootPrim, const PathIndexToSpecIndexMap &psmap,
  //                         Layer *layer);

  bool ToLayer(Layer *layer);

  ///
  /// --------------------------------------------------
  ///

  void PushError(const std::string &s) { _err = s + _err; }

  void PushWarn(const std::string &s) { _warn = s + _warn; }

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const {
    if (!crate_reader) {
      return 0;
    }
    return crate_reader->GetMemoryUsageInMB();
  }

  USDCMemoryUsageReport GetMemoryUsageReport() const {
    USDCMemoryUsageReport report;
    if (!crate_reader) {
      return report;
    }

    report.current_usage_bytes = crate_reader->GetMemoryUsageInBytes();
    report.peak_usage_bytes = crate_reader->GetPeakMemoryUsageInBytes();
    report.max_budget_bytes = crate_reader->GetMemoryBudgetInBytes();
    report.remaining_budget_bytes = crate_reader->GetRemainingMemoryBudgetInBytes();
    return report;
  }

 private:
  nonstd::expected<APISchemas, std::string> ToAPISchemas(
      const ListOp<value::token> &, bool ignore_unknown, std::string &warn);

  // ListOp<T> -> (ListEditOp, [T])
  template <typename T>
  std::vector<std::pair<ListEditQual, std::vector<T>>> DecodeListOp(
      const ListOp<T> &);

  ///
  /// Builds std::map<std::string, Property> from the list of Path(Spec)
  /// indices.
  ///
  bool BuildPropertyMap(const std::vector<size_t> &pathIndices,
                        const PathIndexToSpecIndexMap &psmap,
                        prim::PropertyMap *props);

  bool ResolveFieldValuePairs(
      const crate::Spec &spec,
      const crate::FieldValuePairVector **fvs,
      crate::FieldValuePairVector *scratch);

  bool ReconstrcutStageMeta(const crate::FieldValuePairVector &fvs,
                            StageMetas *out);

  Prim *ResolveVariantOwnerPrim(int32_t parent_node_id, int32_t current_node_id,
                                Prim *current_prim, Prim *parent_prim,
                                bool include_current_node);

  bool AttachVariantPrimChildrenToOwner(int32_t owner_node_id, Prim *owner_prim);

  bool IsExpectedNonPrimVariantChild(
      int32_t node_id, const PathIndexToSpecIndexMap &psmap) const;

  bool AddVariantChildrenToPrimNode(
      int32_t parent_prim_idx, int32_t prim_idx, const std::string &variantSetName, const std::vector<value::token> &variantChildren) {
    if (parent_prim_idx < 0) {
      return false;
    }

    if (prim_idx < 0) {
      return false;
    }

    _variantChildren[uint32_t(parent_prim_idx)][prim_idx] = {variantSetName, variantChildren};

    return true;
  }

  bool AddVariantChildrenToVariantNode(
      int32_t parent_variant_idx, int32_t prim_idx, const std::string &variantSetName, const std::vector<value::token> &variantChildren) {
    if (parent_variant_idx < 0) {
      return false;
    }

    if (prim_idx < 0) {
      return false;
    }

    // Store variant children for variant nodes (nested variants)
    _variantChildren[uint32_t(parent_variant_idx)][prim_idx] = {variantSetName, variantChildren};

    return true;
  }

  bool FindVariantSet(int32_t parent_prim_idx, const std::string &variantSetName, uint32_t &spec_path_idx) {
    if (parent_prim_idx < 0) {
      return false;
    }

    for (const auto &it : _variantChildren[uint32_t(parent_prim_idx)]) {
      if (variantSetName == it.second.first) {
        spec_path_idx = uint32_t(it.first);
        return true;
      }
    }

    return false;

  }

  bool AddVariantToPrimNode(int32_t prim_idx, const value::Value &variant);

  crate::CrateReader *crate_reader{nullptr};

  StreamReader *_sr = nullptr;
  std::string _err;
  std::string _warn;

  USDCReaderConfig _config;
  
  usdc::ProgressCallback _progress_callback;
  void *_progress_userptr{nullptr};

  nonstd::optional<Path> GetPath(crate::Index index) const {
    if (index.value < _paths->size()) {
      return (*_paths)[index.value];
    }

    return nonstd::nullopt;
  }

  nonstd::optional<Path> GetElemPath(crate::Index index) const {
    if (index.value < _elemPaths->size()) {
      return (*_elemPaths)[index.value];
    }

    return nonstd::nullopt;
  }

  // Direct output version - avoids optional extraction overhead
  bool GetPathDirect(crate::Index index, Path *out) const {
    if (!out) return false;
    if (index.value < _paths->size()) {
      *out = (*_paths)[index.value];
      return true;
    }
    return false;
  }

  bool GetElemPathDirect(crate::Index index, Path *out) const {
    if (!out) return false;
    if (index.value < _elemPaths->size()) {
      *out = (*_elemPaths)[index.value];
      return true;
    }
    return false;
  }

  // Use const references to avoid copying data from crate_reader when possible
  // NOTE: These are only valid while crate_reader exists
  const std::vector<crate::CrateReader::Node> *_nodes = nullptr;
  const std::vector<crate::Spec> *_specs = nullptr;
  const std::vector<crate::Field> *_fields = nullptr;
  const std::vector<crate::Index> *_fieldset_indices = nullptr;
  const std::vector<crate::Index> *_string_indices = nullptr;
  const std::vector<Path> *_paths = nullptr;
  const std::vector<Path> *_elemPaths = nullptr;

  const crate::LiveFieldSetMap
      *_live_fieldsets = nullptr;  // <fieldset index, List of field with unpacked Values>

  // Budget reserved by the last lazy DecodeFieldSet scratch buffer.
  // Released when scratch is reused or at end of reconstruction.
  uint64_t _scratch_budget_reserved{0};

  // std::vector<PrimNode> _prim_nodes;

  // VariantSet Spec.
  // key = parent idx, value = (prim_id, (variantSetName, variantChildren names))
  std::map<uint32_t, std::unordered_map<int32_t, std::pair<std::string, std::vector<value::token>>>> _variantChildren;

  std::unordered_map<int32_t, Prim> _prims; // For Stage

  // For Prim/Props defined as Variant(SpecType::VariantSet)
  // key = path index.
  std::unordered_map<int32_t, Prim> _variantPrims; // For Stage
  std::map<int32_t, PrimSpec> _variantPrimSpecs; // For Layer
  std::map<int32_t, std::pair<Path, Property>> _variantProps;
  std::map<int32_t, Variant> _variants;

  // key = parent path index, values = keys to `_variantPrims`, `_variantProps`
  std::map<int32_t, std::vector<int32_t>> _variantPrimChildren;
  std::map<int32_t, std::vector<int32_t>> _variantPropChildren;

  // Fast lookup for variant prims by name: {variantSetName, variantName} -> path index
  // Enables O(1) lookup instead of O(n) linear search
  std::map<std::pair<std::string, std::string>, int32_t> _variantNameIndex;

  // Check if given node_id is a prim node.
  std::set<int32_t> _prim_table;

  std::set<std::string> _supported_prim_attr_types;

  // mmap zero-copy support
  MMapArrayTable _mmap_table;
  std::string _current_prim_path;  // Set during reconstruction for mmap table key building
  MMapArrayRef _pending_mmap_ref{};
  bool _has_pending_mmap_ref{false};
};

//
// -- Impl
//


namespace {}

nonstd::expected<APISchemas, std::string> USDCReader::Impl::ToAPISchemas(
    const ListOp<value::token> &arg, bool ignore_unknown, std::string &warn) {
  APISchemas schemas;

  // Use centralized handler from enum-handlers.hh (wrapper for value::token)
  auto SchemaHandler =
      [](const value::token &tok) -> nonstd::optional<APISchemas::APIName> {
    return enum_handler::APISchemaNameOpt(tok.str());
  };

  if (arg.IsExplicit()) {  // fast path
    for (auto &item : arg.GetExplicitItems()) {
      if (auto pv = SchemaHandler(item)) {
        std::string instanceName = "";  // TODO
        schemas.names.push_back({pv.value(), instanceName});
      } else if (ignore_unknown) {
        // Store unknown schema instead of just warning
        std::string instanceName = "";  // TODO: parse instance name if present
        schemas.unknownSchemas.push_back({item.str(), instanceName});
        warn += "Preserving unknown API schema: " + item.str() + "\n";
      } else {
        return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                       item.str());
      }
    }
    schemas.listOpQual = ListEditQual::ResetToExplicit;

  } else {
    // Assume all items have same ListEdit qualifier.
    if (arg.GetExplicitItems().size()) {
      if (arg.GetAddedItems().size() || arg.GetAppendedItems().size() ||
          arg.GetDeletedItems().size() || arg.GetPrependedItems().size() ||
          arg.GetOrderedItems().size()) {
        return nonstd::make_unexpected(
            "Currently TinyUSDZ does not support ListOp with different "
            "ListEdit qualifiers.");
      }
      for (auto &&item : arg.GetExplicitItems()) {
        if (auto pv = SchemaHandler(item)) {
          std::string instanceName = "";  // TODO
          schemas.names.push_back({pv.value(), instanceName});
        } else if (ignore_unknown) {
          // Store unknown schema instead of just warning
          std::string instanceName = "";  // TODO: parse instance name if present
          schemas.unknownSchemas.push_back({item.str(), instanceName});
          warn += "Preserving unknown API schema: " + item.str() + "\n";
        } else {
          return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                         item.str());
        }
      }
      schemas.listOpQual = ListEditQual::ResetToExplicit;

    } else if (arg.GetAddedItems().size()) {
      if (arg.GetExplicitItems().size() || arg.GetAppendedItems().size() ||
          arg.GetDeletedItems().size() || arg.GetPrependedItems().size() ||
          arg.GetOrderedItems().size()) {
        return nonstd::make_unexpected(
            "Currently TinyUSDZ does not support ListOp with different "
            "ListEdit qualifiers.");
      }
      for (auto &item : arg.GetAddedItems()) {
        if (auto pv = SchemaHandler(item)) {
          std::string instanceName = "";  // TODO
          schemas.names.push_back({pv.value(), instanceName});
        } else if (ignore_unknown) {
          // Store unknown schema instead of just warning
          std::string instanceName = "";  // TODO: parse instance name if present
          schemas.unknownSchemas.push_back({item.str(), instanceName});
          warn += "Preserving unknown API schema: " + item.str() + "\n";
        } else {
          return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                         item.str());
        }
      }
      schemas.listOpQual = ListEditQual::Add;
    } else if (arg.GetAppendedItems().size()) {
      if (arg.GetExplicitItems().size() || arg.GetAddedItems().size() ||
          arg.GetDeletedItems().size() || arg.GetPrependedItems().size() ||
          arg.GetOrderedItems().size()) {
        return nonstd::make_unexpected(
            "Currently TinyUSDZ does not support ListOp with different "
            "ListEdit qualifiers.");
      }
      for (auto &&item : arg.GetAppendedItems()) {
        if (auto pv = SchemaHandler(item)) {
          std::string instanceName = "";  // TODO
          schemas.names.push_back({pv.value(), instanceName});
        } else if (ignore_unknown) {
          // Store unknown schema instead of just warning
          std::string instanceName = "";  // TODO: parse instance name if present
          schemas.unknownSchemas.push_back({item.str(), instanceName});
          warn += "Preserving unknown API schema: " + item.str() + "\n";
        } else {
          return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                         item.str());
        }
      }
      schemas.listOpQual = ListEditQual::Append;
    } else if (arg.GetDeletedItems().size()) {
      if (arg.GetExplicitItems().size() || arg.GetAddedItems().size() ||
          arg.GetAppendedItems().size() || arg.GetPrependedItems().size() ||
          arg.GetOrderedItems().size()) {
        return nonstd::make_unexpected(
            "Currently TinyUSDZ does not support ListOp with different "
            "ListEdit qualifiers.");
      }
      for (auto &&item : arg.GetDeletedItems()) {
        if (auto pv = SchemaHandler(item)) {
          std::string instanceName = "";  // TODO
          schemas.names.push_back({pv.value(), instanceName});
        } else if (ignore_unknown) {
          // Store unknown schema instead of just warning
          std::string instanceName = "";  // TODO: parse instance name if present
          schemas.unknownSchemas.push_back({item.str(), instanceName});
          warn += "Preserving unknown API schema: " + item.str() + "\n";
        } else {
          return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                         item.str());
        }
      }
      schemas.listOpQual = ListEditQual::Delete;
    } else if (arg.GetPrependedItems().size()) {
      if (arg.GetExplicitItems().size() || arg.GetAddedItems().size() ||
          arg.GetAppendedItems().size() || arg.GetDeletedItems().size() ||
          arg.GetOrderedItems().size()) {
        return nonstd::make_unexpected(
            "Currently TinyUSDZ does not support ListOp with different "
            "ListEdit qualifiers.");
      }
      for (auto &&item : arg.GetPrependedItems()) {
        if (auto pv = SchemaHandler(item)) {
          std::string instanceName = "";  // TODO
          schemas.names.push_back({pv.value(), instanceName});
        } else if (ignore_unknown) {
          // Store unknown schema instead of just warning
          std::string instanceName = "";  // TODO: parse instance name if present
          schemas.unknownSchemas.push_back({item.str(), instanceName});
          warn += "Preserving unknown API schema: " + item.str() + "\n";
        } else {
          return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                         item.str());
        }
      }
      schemas.listOpQual = ListEditQual::Prepend;
    } else if (arg.GetOrderedItems().size()) {
      if (arg.GetExplicitItems().size() || arg.GetAddedItems().size() ||
          arg.GetAppendedItems().size() || arg.GetDeletedItems().size() ||
          arg.GetPrependedItems().size()) {
        return nonstd::make_unexpected(
            "Currently TinyUSDZ does not support ListOp with different "
            "ListEdit qualifiers.");
      }

      // schemas.qual = ListEditQual::Order;
      return nonstd::make_unexpected("TODO: Ordered ListOp items.");
    } else {
      // ??? This should not happend.
      return nonstd::make_unexpected("Internal error: ListOp conversion.");
    }
  }

  return std::move(schemas);
}

template <typename T>
std::vector<std::pair<ListEditQual, std::vector<T>>>
USDCReader::Impl::DecodeListOp(const ListOp<T> &arg) {
  std::vector<std::pair<ListEditQual, std::vector<T>>> dst;

  if (arg.IsExplicit()) {  // fast path
    dst.push_back({ListEditQual::ResetToExplicit, arg.GetExplicitItems()});
  } else {
    // Assume all items have same ListEdit qualifier.
    if (arg.GetExplicitItems().size()) {
      dst.push_back({ListEditQual::ResetToExplicit, arg.GetExplicitItems()});
    }
    if (arg.GetAddedItems().size()) {
      dst.push_back({ListEditQual::Add, arg.GetAddedItems()});
    }
    if (arg.GetAppendedItems().size()) {
      dst.push_back({ListEditQual::Append, arg.GetAppendedItems()});
    }
    if (arg.GetDeletedItems().size()) {
      dst.push_back({ListEditQual::Delete, arg.GetDeletedItems()});
    }
    if (arg.GetPrependedItems().size()) {
      dst.push_back({ListEditQual::Prepend, arg.GetPrependedItems()});
    }
    if (arg.GetOrderedItems().size()) {
      dst.push_back({ListEditQual::Order, arg.GetOrderedItems()});
    }
  }

  return dst;
}

bool USDCReader::Impl::ResolveFieldValuePairs(
    const crate::Spec &spec, const crate::FieldValuePairVector **fvs,
    crate::FieldValuePairVector *scratch) {
  if (!fvs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Internal error: `fvs` is nullptr.");
  }

  if (_config.use_lazy_property_construction) {
    if (!scratch) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Internal error: `scratch` is nullptr.");
    }

    if (!crate_reader) {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "Internal error: crate reader is nullptr.");
    }

    // Release budget reserved by the previous DecodeFieldSet call before
    // clearing the scratch buffer (scratch->clear() frees the actual memory).
    if (_scratch_budget_reserved > 0) {
      crate_reader->ReleaseMemoryBudget(_scratch_budget_reserved);
      _scratch_budget_reserved = 0;
    }

    scratch->clear();

    uint64_t budget_before = crate_reader->GetMemoryUsageInBytes();
    if (!crate_reader->DecodeFieldSet(spec.fieldset_index, scratch)) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "Failed to decode fieldset id: " +
                    std::to_string(spec.fieldset_index.value));
    }
    _scratch_budget_reserved =
        crate_reader->GetMemoryUsageInBytes() - budget_before;

    (*fvs) = scratch;
    return true;
  }

  if (!_live_fieldsets) {
    PUSH_ERROR_AND_RETURN_TAG(kTag,
                              "Internal error: live fieldsets is nullptr.");
  }

  if (!_live_fieldsets->count(spec.fieldset_index)) {
    PUSH_ERROR_AND_RETURN_TAG(
        kTag, "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                  " must exist in live fieldsets.");
  }

  (*fvs) = &(_live_fieldsets->at(spec.fieldset_index));
  return true;
}

Prim *USDCReader::Impl::ResolveVariantOwnerPrim(int32_t parent_node_id,
                                                int32_t current_node_id,
                                                Prim *current_prim,
                                                Prim *parent_prim,
                                                bool include_current_node) {
  if (include_current_node) {
    if (current_prim) {
      return current_prim;
    }

    auto current_it = _variantPrims.find(current_node_id);
    if (current_it != _variantPrims.end()) {
      return &(current_it->second);
    }
  }

  if (parent_prim) {
    return parent_prim;
  }

  auto parent_it = _variantPrims.find(parent_node_id);
  if (parent_it != _variantPrims.end()) {
    return &(parent_it->second);
  }

  // Fallback for malformed trees.
  if (!include_current_node) {
    if (current_prim) {
      return current_prim;
    }

    auto current_it = _variantPrims.find(current_node_id);
    if (current_it != _variantPrims.end()) {
      return &(current_it->second);
    }
  }

  return nullptr;
}

bool USDCReader::Impl::AttachVariantPrimChildrenToOwner(int32_t owner_node_id,
                                                        Prim *owner_prim) {
  if (!_variantPrimChildren.count(owner_node_id)) {
    return true;
  }

  if (!owner_prim) {
    PUSH_ERROR_AND_RETURN("Internal error: variant owner Prim is null.");
  }

  DCOUT(fmt::format("{} has variant Prim ", owner_prim->element_name()));

  for (const auto &item : _variantPrimChildren.at(owner_node_id)) {
    auto vp_it = _variantPrims.find(item);
    if (vp_it == _variantPrims.end()) {
      PUSH_ERROR_AND_RETURN("Internal error: variant Prim children not found.");
    }

    Prim &vp = vp_it->second;
    DCOUT(fmt::format("  variantPrim name {}", vp.element_name()));

    // element_name must be variant: "{variant=value}"
    if (!is_variantElementName(vp.element_name())) {
      PUSH_ERROR_AND_RETURN(
          "Corrupted Crate. Variant Prim has invalid element_name.");
    }

    std::array<std::string, 2> toks;
    if (!tokenize_variantElement(vp.element_name(), &toks)) {
      PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
    }

    std::string variantSetName = toks[0];
    std::string variantName = toks[1];

    VariantSet &vs = owner_prim->variantSets()[variantSetName];
    if (vs.name.empty()) {
      vs.name = variantSetName;
    }

    Variant &variant = vs.variantSet[variantName];
    variant.metas() = vp.metas();
    variant.primChildren() = vp.children();

    // Preserve nested variantSets carried by this variant Prim.
    if (!vp.variantSets().empty()) {
      variant.variantSets() = vp.variantSets();
    }
  }

  return true;
}

bool USDCReader::Impl::IsExpectedNonPrimVariantChild(
    int32_t node_id, const PathIndexToSpecIndexMap &psmap) const {
  if (_variantPrimChildren.count(node_id) || _variantPropChildren.count(node_id)) {
    return true;
  }

  auto psit = psmap.find(uint32_t(node_id));
  if (psit == psmap.end()) {
    return true;  // Some container nodes may not have a direct spec entry.
  }

  if (psit->second >= _specs->size()) {
    return false;
  }

  SpecType st = (*_specs)[psit->second].spec_type;
  return ((st == SpecType::Variant) || (st == SpecType::VariantSet) ||
          (st == SpecType::Attribute) ||
          (st == SpecType::Connection) || (st == SpecType::Relationship) ||
          (st == SpecType::RelationshipTarget));
}

bool USDCReader::Impl::BuildPropertyMap(const std::vector<size_t> &pathIndices,
                                        const PathIndexToSpecIndexMap &psmap,
                                        prim::PropertyMap *props) {

  for (size_t i = 0; i < pathIndices.size(); i++) {
    int child_index = int(pathIndices[i]);
    if ((child_index < 0) || (child_index >= int(_nodes->size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes->size()) +
                 ")");
      return false;
    }

    if (!psmap.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index = psmap.at(uint32_t(child_index));
    if (spec_index >= _specs->size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs->size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = (*_specs)[spec_index];

    // Property must be Attribute or Relationship
    if ((spec.spec_type == SpecType::Attribute) ||
        (spec.spec_type == SpecType::Relationship)) {
      // OK
    } else {
      continue;
    }

    nonstd::optional<Path> path = GetPath(spec.path_index);

    if (!path) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
    }

    DCOUT("Path prim part: " << path.value().prim_part()
                             << ", prop part: " << path.value().prop_part()
                             << ", spec_index = " << spec_index);

    crate::FieldValuePairVector decoded_fvs;
    const crate::FieldValuePairVector *child_fvs = nullptr;
    if (!ResolveFieldValuePairs(spec, &child_fvs, &decoded_fvs)) {
      return false;
    }

    {
      std::string prop_name = path.value().prop_part();
      if (prop_name.empty()) {
        DCOUT("path = " << dump_path(path.value()));
        // ???
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Property Prop.PropPart is empty");
      }

      std::string prop_err;
      if (!pathutil::ValidatePropPath(Path("", prop_name), &prop_err)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid Property name `{}`: {}", prop_name, prop_err));
      }

      Property prop;
      // In lazy mode, child_fvs points to local decoded_fvs — safe to move from.
      if (!ParseProperty(spec.spec_type,
                         const_cast<crate::FieldValuePairVector &>(*child_fvs),
                         &prop, _config.use_lazy_property_construction)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            fmt::format(
                "Failed to construct Property `{}` from FieldValuePairVector.",
                prop_name));
      }

      (*props)[prop_name] = std::move(prop);
      DCOUT("Add property : " << prop_name);

      // Record mmap ref with full path key
      if (_has_pending_mmap_ref) {
        std::string prim_path = path.value().prim_part();
        _mmap_table.add(prim_path, prop_name, _pending_mmap_ref);
        _has_pending_mmap_ref = false;
      }
    }
  }

  return true;
}


/// Property fieldSet example
///
///   specTyppe = SpecTypeAttribute
///
///     - typeName(token) : type name of Attribute(e.g. `float`)
///     - custom(bool) : `custom` qualifier
///     - variability(variability) : Variability(meta?)
///     <value>
///       - default : Default(fallback) value.
///       - timeSample(TimeSamples) : `.timeSamples` data.
///       - connectionPaths(type = ListOpPath) : `.connect`
///       - (Empty) : Define only(Neiher connection nor value assigned. e.g.
///       "float outputs:rgb")
bool USDCReader::Impl::ParseProperty(const SpecType spec_type,
                                     crate::FieldValuePairVector &fvs,
                                     Property *prop,
                                     bool allow_move_from_fvs) {
  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  if (!prop) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Internal error. prop is nullptr.");
  }

  bool custom{false};
  nonstd::optional<value::token> typeName;
  nonstd::optional<Interpolation> interpolation;
  nonstd::optional<int> elementSize;
  nonstd::optional<bool> hidden;
  nonstd::optional<CustomDataType> customData;
  nonstd::optional<double> weight;
  nonstd::optional<value::token> bindMaterialAs;
  nonstd::optional<value::token> connectability;
  nonstd::optional<value::token> renderType;
  nonstd::optional<value::token> outputName;
  nonstd::optional<CustomDataType> sdrMetadata;
  nonstd::optional<value::StringData> comment;
  nonstd::optional<Variability> variability;
  AttrMeta meta; // for other not frequently-used attribute/relationship metadata.
  //Property::Type propType{Property::Type::EmptyAttrib};
  Attribute attr;

  nonstd::optional<value::Value> defaultValue;
  Relationship rel;

  // for attribute
  bool isValueBlock{false};
  bool hasDefault{false};
  bool hasTimeSamples{false};
  bool hasConnectionPaths{false};

  // for relationship
  bool hasTargetPaths{false};

  // metadata (hasConnectionChildren/hasTargetChildren removed — were only
  // used by commented-out code)

  DCOUT("== List of Fields");

  primvar::PrimVar var;

  // first detect typeName
  for (auto &fv : fvs) {
    if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("  typeName = " << pv.value().str());
        typeName = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    }
  }

  if (typeName) { // this should be always true though.
    attr.set_type_name(typeName.value().str());
  }

  for (auto &fv : fvs) {
    DCOUT(" fv name " << fv.first << "(type = " << fv.second.type_name()
                      << ")");

    // Debug: Check timeSamples field specifically
    if (fv.first.find("time") != std::string::npos) {
      DCOUT(">>> DEBUG: Found field with 'time' in name: '" << fv.first << "', length = " << fv.first.size());
      //bool matches = (fv.first == "timeSamples");
      //DCOUT(">>> Comparing with 'timeSamples': matches = " << matches);
    }

    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        custom = pv.value();
        DCOUT("  custom = " << pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "variability") {
      if (auto pv = fv.second.get_value<Variability>()) {
        variability = pv.value();
        DCOUT("  variability = " << to_string(variability.value()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variability` field is not `varibility` type.");
      }
    } else if (fv.first == "typeName") {
      // 'typeName' is already processed. nothing to do here.
      continue;
    } else if (fv.first == "default") {
      //propType = Property::Type::Attrib;

      // Set scalar(non-timesampled) value
      //TUSDZ_LOG_I("defaultValue");

      // Check for mmap ref before move
      if (_config.mmap_zero_copy && fv.second.has_mmap_ref()) {
        _pending_mmap_ref = fv.second.mmap_ref();
        _has_pending_mmap_ref = true;
      }

      // In lazy mode (allow_move_from_fvs=true), fvs is a local scratch
      // buffer so we can move large arrays directly. Otherwise copy
      // because shared fieldsets may be referenced by other specs.
      if (allow_move_from_fvs) {
        defaultValue = std::move(fv.second.get_raw());
      } else {
        const value::Value &raw = fv.second.get_raw();
        defaultValue = raw;
      }
      //TUSDZ_LOG_I("defaultValue end");
      hasDefault = true;

      // TODO: Handle UnregisteredValue in crate-reader.cc
      // UnregisteredValue is represented as string.
      if (const auto pv = defaultValue.value().get_value<std::string>()) {
        if (typeName && (typeName.value().str() != "string")) {
          if (IsUnregisteredValueType(typeName.value().str())) {
            DCOUT("UnregisteredValue type: " << typeName.value().str());

            std::string local_err;
            value::Value v;
            if (!ascii::ParseUnregistredValue(typeName.value().str(), pv.value(), &v, &local_err)) {
              PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse UnregisteredValue string with type `{}`: {}", typeName.value().str(), local_err));
            }

            defaultValue = std::move(v);
          }
        }
      }

    } else if (fv.first == "timeSamples") {
      //propType = Property::Type::Attrib;
      DCOUT(">>> Entering timeSamples block");

      hasTimeSamples = true;

      //if (auto pv = fv.second.get_value<value::TimeSamples>()) {
      if (const value::TimeSamples *vptr = fv.second.as<value::TimeSamples>()) {
        // DANGER:
        // TODO: remove const from func arg
        value::TimeSamples &ts = *(const_cast<value::TimeSamples *>(vptr));

        DCOUT("ts.type_id " << ts.type_id());

        // If TimeSamples is uninitialized (all samples were VALUE_BLOCK),
        // initialize it with the type from the attribute's typeName
        if (ts.type_id() == 0 && typeName) {
          uint32_t type_id = value::GetTypeId(typeName.value().str());

          if (type_id == value::TYPE_ID_INVALID) {
            PUSH_ERROR_AND_RETURN(fmt::format("Invalid typeName `{}` for TimeSamples", typeName.value().str()));
          }

          if (!ts.init(type_id)) {
            PUSH_ERROR_AND_RETURN(fmt::format("Failed to initialize TimeSamples with type_id {} for typeName `{}`", type_id, typeName.value().str()));
          }
        }

        DCOUT("set_timesamples");

        // In lazy mode, fvs is a local scratch buffer (decoded_fvs) — safe to move.
        // In non-lazy mode, fvs points into shared _live_fieldsets — must copy.
        value::TimeSamples ts_final;
        if (allow_move_from_fvs) {
          ts_final = std::move(ts);
        } else {
          ts_final = ts;  // deep copy
        }

        // Apply role type casting if typeName specifies a role type
        // (e.g., cast float3 to color3f, point3f, etc.)
        if (typeName) {
          uint32_t role_type_id = value::GetTypeId(typeName.value().str());
          if (role_type_id != value::TYPE_ID_INVALID) {
            if (ts_final.cast_to_role_type(role_type_id)) {
              DCOUT(fmt::format("Cast TimeSamples to role type {}", typeName.value().str()));
            }
            // It's ok if casting fails - the base type is still valid
          }
        }

        var.set_timesamples(std::move(ts_final));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`timeSamples` is not TimeSamples data.");
      }
    } else if (fv.first == "interpolation") {
      //propType = Property::Type::Attrib;

      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("  interpolation = " << pv.value().str());

        if (auto interp = InterpolationFromString(pv.value().str())) {
          interpolation = interp.value();
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid token for `interpolation`.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`interpolation` field is not `token` type.");
      }
    } else if (fv.first == "connectionPaths") {
      // Attribute connection(.connect)
      //propType = Property::Type::Connection;
      hasConnectionPaths = true;

      if (auto pv = fv.second.get_value<ListOp<Path>>()) {
        auto p = pv.value();
        DCOUT("connectionPaths = " << to_string(p));

        if (!p.IsExplicit()) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "`connectionPaths` must be composed of Explicit items.");
        }

        // Must be explicit_items for now.
        auto items = p.GetExplicitItems();
        if (items.size() == 0) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "`connectionPaths` have empty Explicit items.");
        }

        attr.set_connections(items); 

      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`connectionPaths` field is not `ListOp[Path]` type.");
      }
    } else if (fv.first == "targetPaths") {
      // `rel`
      //propType = Property::Type::Relation;
      hasTargetPaths = true;

      // Check for ValueBlock first (rel ... = None)
      if (fv.second.get_value<value::ValueBlock>()) {
        // Relationship is blocked (None)
        rel.set_blocked();
        DCOUT("targetPaths = None (blocked)");
      } else if (auto pv = fv.second.get_value<ListOp<Path>>()) {
        const ListOp<Path> &p = pv.value();
        DCOUT("targetPaths = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        if (ps.empty()) {
          // Empty `targetPaths`
          PUSH_ERROR_AND_RETURN_TAG(kTag, "`targetPaths` is empty.");
        }

        if (ps.size() > 1) {
          // This should not happen though.
          PUSH_WARN(
              "ListOp with multiple ListOpType is not supported for now. Use "
              "the first one: " +
              to_string(std::get<0>(ps[0])));
        }

        auto qual = std::get<0>(ps[0]);
        auto items = std::get<1>(ps[0]);

        if (items.size() == 1) {
          // Single
          const Path path = items[0];

          rel.set(path);

        } else {
          rel.set(items);  // [Path]
        }

        rel.set_listedit_qual(qual);

      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`targetPaths` field is not `ListOp[Path]` or ValueBlock type.");
      }

    } else if (fv.first == "hidden") {
      // Attribute hidden param
      if (auto pv = fv.second.get_value<bool>()) {
        auto p = pv.value();
        DCOUT("hidden = " << to_string(p));

        hidden = p;

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`elementSize` field is not `int` type.");
      }
    } else if (fv.first == "elementSize") {
      // Attribute Meta
      if (auto pv = fv.second.get_value<int>()) {
        auto p = pv.value();
        DCOUT("elementSize = " << to_string(p));

        if ((p < 1) || (uint32_t(p) > _config.kMaxElementSize)) {
          PUSH_WARN(
              fmt::format("`elementSize` too large. Must be within [{}, {}), but got {}",
                          1, _config.kMaxElementSize, p));
        }

        elementSize = p;

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`elementSize` field is not `int` type.");
      }
    } else if (fv.first == "weight") {
      // pxrUSD uses float type.
      if (auto pv = fv.second.get_value<float>()) {
        auto p = pv.value();
        DCOUT("weight = " << p);

        weight = double(p);

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`weight` field is not `float` type.");
      }
    } else if (fv.first == "bindMaterialAs") {
      // Attribute Meta
      if (auto pv = fv.second.get_value<value::token>()) {
        auto p = pv.value();
        DCOUT("bindMaterialAs = " << to_string(p));

        if ((p.str() == kWeakerThanDescendants) || (p.str() == kStrongerThanDescendants)) {
          // ok
        } else {
          // still any token is valid(for future usecase)
          PUSH_WARN("Unsupported bindMaterialAs token: " << p.str());
        }
        bindMaterialAs = p;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`bindMaterialAs` field is not `token` type.");
      }
    } else if (fv.first == "targetChildren") {
      // `targetChildren` seems optionally exist to validate the existence of
      // target Paths when `targetPaths` field exists.
      // TODO: validate path of `targetChildren`
      // Path vector
      if (auto pv = fv.second.get_value<std::vector<Path>>()) {
        DCOUT("targetChildren = " << pv.value());
        // PUSH_WARN("TODO: targetChildren");

      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`targetChildren` field is not `PathVector` type.");
      }
    } else if (fv.first == "connectionChildren") {
      // `connectionChildren` seems optionally exist to validate the existence
      // of connection Paths when `connectiontPaths` field exists.
      // TODO: validate path of `connetionChildren`
      // Path vector
      if (auto pv = fv.second.get_value<std::vector<Path>>()) {
        DCOUT("connectionChildren = " << pv.value());
        // PUSH_WARN("TODO: connectionChildren");
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`connectionChildren` field is not `PathVector` type.");
      }
    } else if (fv.first == "connectability") {
      if (auto pv = fv.second.get_value<value::token>()) {
        connectability = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`connectability` must be type `token`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "outputName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        outputName = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`outputName` must be type `token`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "renderType") {
      if (auto pv = fv.second.get_value<value::token>()) {
        renderType = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`renderType` must be type `token`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "sdrMetadata") {
      if (auto pv = fv.second.get_value<CustomDataType>()) {
        sdrMetadata = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`sdrMetadata` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "customData") {
      // CustomData(dict)
      if (auto pv = fv.second.get_value<CustomDataType>()) {
        customData = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`customData` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "comment") {
      if (auto pv = fv.second.get_value<std::string>()) {
        value::StringData s;
        s.value = pv.value();
        s.is_triple_quoted = hasNewline(s.value);
        comment = s;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`comment` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "colorSpace") {
      if (auto pv = fv.second.get_value<value::token>()) {
        meta.set_colorSpace(pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`colorSpace` must be type `token`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "displayName") {
      if (auto pv = fv.second.get_value<std::string>()) {
        meta.set_displayName(pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`displayName` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "displayGroup") {
      if (auto pv = fv.second.get_value<std::string>()) {
        meta.set_displayGroup(pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`displayGroup` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "unauthoredValuesIndex") {
      if (auto pv = fv.second.get_value<int>()) {
        meta.set_unauthoredValuesIndex(pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`unauthoredValuesIndex` must be type `int`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else {
      // TODO: register unkown metadataum as custom metadata?
      PUSH_WARN("TODO: " << fv.first);
      DCOUT("TODO: " << fv.first);
    }
  }
  DCOUT("== End List of Fields");

  // Post check

  // Do role type cast for default value.
  // (NOTE: role type cast for timeSamples is done earlier when processing timeSamples field)
  if (defaultValue.has_value()) {
    if (typeName) {
      if (defaultValue.value().type_id() == value::TypeTraits<value::ValueBlock>::type_id()) {
        // nothing to do
      } else {
        std::string reqTy = typeName.value().str();
        std::string scalarTy = defaultValue.value().type_name();

        if (reqTy.compare(scalarTy) != 0) {

          // Some inlined? value uses less accuracy type(e.g. `half3`) than
          // typeName(e.g. `float3`) Use type specified in `typeName` as much as
          // possible.
          bool ret = value::UpcastType(reqTy, defaultValue.value());
          if (ret) {
            DCOUT(fmt::format("Upcast type from {} to {}.", scalarTy, reqTy));
          }

          // Optionally, cast to role type(in crate data, `typeName` uses role typename(e.g. `color3f`), whereas stored data uses base typename(e.g. VEC3F)
          scalarTy = defaultValue.value().type_name();
          if (value::RoleTypeCast(value::GetTypeId(reqTy), defaultValue.value())) {
            DCOUT(fmt::format("Casted to Role type {} from type {}.", reqTy, scalarTy));
          } else {
            // Its ok.
          }
        }
      }
    }
    // Check type before moving
    bool is_value_block = (defaultValue.value().type_id() == value::TypeTraits<value::ValueBlock>::type_id());
    var.set_value(std::move(defaultValue.value()));

    if (is_value_block) {
      isValueBlock = true;
    }
  }

  // HACK
  attr.set_var(std::move(var));

  if (isValueBlock) {
    // attr's type is replaced with ValueBlock type  by `set_var`, so overwrite type with typeName
    if (typeName) {
      // Use `typeName`
      attr.set_type_name(typeName.value().str());
    }
  }

  // Attribute metas
  {
    if (interpolation) {
      meta.set_interpolation_enum(interpolation.value());
    }
    if (elementSize) {
      meta.set_elementSize(static_cast<uint32_t>(elementSize.value()));
    }
    if (hidden) {
      meta.set_hidden(hidden.value());
    }
    if (customData) {
      meta.set_customData(customData.value());
    }
    if (weight) {
      meta.set_weight(weight.value());
    }
    if (comment) {
      meta.set_comment(comment.value());
    }
    if (bindMaterialAs) {
      meta.set_bindMaterialAs(bindMaterialAs.value());
    }
    if (outputName) {
      meta.set_outputName(outputName.value());
    }
    if (sdrMetadata) {
      meta.set_sdrMetadata(sdrMetadata.value());
    }
    if (connectability) {
      meta.set_connectability(connectability.value());
    }
    if (renderType) {
      meta.set_renderType(renderType.value());
    }
  }



  if (hasTargetPaths) {
    // Relationship

    // TODO: Report as error?
    if (hasDefault) {
      PUSH_WARN("Relationship property has `default` field. Ignore `default` field.");
    }

    if (hasTimeSamples) {
      PUSH_WARN("Relationship property has `timeSamples` field. Ignore `timeSamples` field.");
    }

    if (hasConnectionPaths) {
      PUSH_WARN("Relationship property has `connectionPaths` field. Ignore `connectionPaths` field.");
    }

    if (variability) {
      if (variability.value() == Variability::Varying) {
        rel.set_varying_authored();
      }
    }
    rel.metas() = std::move(meta);  // Move instead of copy
    (*prop) = Property(std::move(rel), custom);
  } else if (hasDefault || hasTimeSamples || hasConnectionPaths) {

    // Attribute
    if (hasTargetPaths) {
      PUSH_WARN("Attribute property has `targetPaths` field. Ignore `targetPaths` field.");
    }

    if (variability) {
      attr.variability() = variability.value();
    }
    attr.metas() = std::move(meta);  // Move instead of copy
    (*prop) = Property(std::move(attr), custom);

  } else {

    // FIXME: SpecType supercedes propType.

    if (typeName) {
      // declare only attribute, e.g.: float myval
      // typeName may be array type.
      std::string baseTypeName = typeName.value().str();
      if (endsWith(baseTypeName, "[]")) {
        baseTypeName = removeSuffix(baseTypeName, "[]");
      }

      // Assume Attribute
      if (!_supported_prim_attr_types.count(baseTypeName)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid or unsupported `typeName` {}", typeName.value()));
      }

      Property p;
      p.set_property_type(Property::Type::EmptyAttrib);
      p.attribute().set_type_name(typeName.value().str());
      p.set_custom(custom);

      if (variability) {
        p.attribute().variability() = variability.value();
      }
      p.attribute().metas() = std::move(meta);  // Move instead of copy

      (*prop) = std::move(p);  // Move instead of copy

    } else {
      DCOUT("spec_type = " << to_string(spec_type));
      if (spec_type == SpecType::Relationship) {
        // `rel` with no target. e.g. `rel target`
        rel = Relationship();
        rel.set_novalue();
        if (variability == Variability::Varying) {
          rel.set_varying_authored();
        }
        rel.metas() = std::move(meta);  // Move instead of copy
        (*prop) = Property(std::move(rel), custom);  // Move instead of copy
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`typeName` field is missing.");
      }
    }
  }

  return true;
}

template <typename T>
bool USDCReader::Impl::ReconstructPrim(const Specifier &spec, const crate::CrateReader::Node &node,
                                       const PathIndexToSpecIndexMap &psmap,
                                       T *prim) {
  // Prim's properties are stored in its children nodes.
  prim::PropertyMap properties;
  if (!BuildPropertyMap(node.GetChildren(), psmap, &properties)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
  }

  prim::ReferenceList refs;  // dummy

  prim::PrimReconstructOptions reconstruct_options;
  reconstruct_options.strict_allowedToken_check = _config.strict_allowedToken_check;

  if (!prim::ReconstructPrim<T>(spec, properties, refs, prim, &_warn, &_err, reconstruct_options)) {
    return false;
  }

  return true;
}

bool USDCReader::Impl::ReconstrcutStageMeta(
    const crate::FieldValuePairVector &fvs, StageMetas *metas) {
  /// Stage(toplevel layer) Meta fieldSet example.
  ///
  ///   specTy = SpecTypePseudoRoot
  ///
  ///     - subLayers(+ subLayerOffsets)
  ///     - customLayerData(dict)
  ///     - defaultPrim(token)
  ///     - metersPerUnit(double)
  ///     - kilogramsPerUnit(double)
  ///     - timeCodesPerSecond(double)
  ///     - upAxis(token)
  ///     - documentation(string) : `doc`
  ///     - comment(string) : comment
  ///     - primChildren(token[]) : Crate only. List of root prims(Root Prim should be traversed based on this array)

  std::vector<std::string> subLayers;
  std::vector<LayerOffset> subLayerOffsets;

  for (const auto &fv : fvs) {
    if (fv.first == "upAxis") {
      auto vt = fv.second.get_value<value::token>();
      if (!vt) {
        PUSH_ERROR_AND_RETURN("`upAxis` must be `token` type.");
      }

      std::string v = vt.value().str();
      if (v == "Y") {
        metas->upAxis = Axis::Y;
      } else if (v == "Z") {
        metas->upAxis = Axis::Z;
      } else if (v == "X") {
        metas->upAxis = Axis::X;
      } else {
        PUSH_ERROR_AND_RETURN("`upAxis` must be 'X', 'Y' or 'Z' but got '" + v +
                              "'(note: Case sensitive)");
      }
      DCOUT("upAxis = " << to_string(metas->upAxis.get_value()));

    } else if (fv.first == "metersPerUnit") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->metersPerUnit = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->metersPerUnit = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`metersPerUnit` value must be double or float type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("metersPerUnit = " << metas->metersPerUnit.get_value());
    } else if (fv.first == "kilogramsPerUnit") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->kilogramsPerUnit = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->kilogramsPerUnit = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`kilogramsPerUnit` value must be double or float type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("kilogramsPerUnit = " << metas->kilogramsPerUnit.get_value());
    } else if (fv.first == "timeCodesPerSecond") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->timeCodesPerSecond = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->timeCodesPerSecond = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`timeCodesPerSecond` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("timeCodesPerSecond = " << metas->timeCodesPerSecond.get_value());
    } else if (fv.first == "startTimeCode") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->startTimeCode = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->startTimeCode = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`startTimeCode` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("startimeCode = " << metas->startTimeCode.get_value());
    } else if (fv.first == "subLayers") {
      if (auto vs = fv.second.get_value<std::vector<std::string>>()) {
        subLayers = vs.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`subLayers` value must be string[] "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
    } else if (fv.first == "subLayerOffsets") {
      if (auto vs = fv.second.get_value<std::vector<LayerOffset>>()) {
        subLayerOffsets = vs.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`subLayerOffsets` value must be LayerOffset[] "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
    } else if (fv.first == "endTimeCode") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->endTimeCode = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->endTimeCode = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`endTimeCode` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("endTimeCode = " << metas->endTimeCode.get_value());
    } else if (fv.first == "framesPerSecond") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->framesPerSecond = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->framesPerSecond = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`framesPerSecond` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("framesPerSecond = " << metas->framesPerSecond.get_value());
    } else if (fv.first == "autoPlay") {
      if (auto vf = fv.second.get_value<bool>()) {
        metas->autoPlay = vf.value();
      } else if (auto vs = fv.second.get_value<std::string>()) {
        // unregisteredvalue uses string type.
        bool autoPlay{true};
        if (vs.value() == "true") {
          autoPlay = true;
        } else if (vs.value() == "false") {
          autoPlay = false;
        } else {
          PUSH_ERROR_AND_RETURN(
              "Unsupported value for `autoPlay`: " << vs.value());
        }
        metas->autoPlay = autoPlay;
      } else {
        PUSH_ERROR_AND_RETURN(
            "`autoPlay` value must be bool "
            "type or string type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("autoPlay = " << metas->autoPlay.get_value());
    } else if (fv.first == "playbackMode") {
      if (auto vf = fv.second.get_value<value::token>()) {
        if (vf.value().str() == "none") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeNone;
        } else if (vf.value().str() == "loop") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeLoop;
        } else {
          PUSH_ERROR_AND_RETURN("Unsupported token value for `playbackMode`.");
        }
      } else if (auto vs = fv.second.get_value<std::string>()) {
        // unregisteredvalue uses string type.
        // Strip surrounding quotes if present (UnregisteredValue encoding
        // preserves the literal quotes from the USDA source).
        std::string val = vs.value();
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
          val = val.substr(1, val.size() - 2);
        }
        if (val == "none") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeNone;
        } else if (val == "loop") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeLoop;
        } else {
          PUSH_ERROR_AND_RETURN(
              "Unsupported value for `playbackMode`: " << val);
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`playbackMode` value must be token "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
    } else if ((fv.first == "defaultPrim")) {
      auto v = fv.second.get_value<value::token>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("`defaultPrim` must be `token` type.");
      }

      metas->defaultPrim = v.value();
      DCOUT("defaultPrim = " << metas->defaultPrim.str());
    } else if (fv.first == "customLayerData") {
      if (auto v = fv.second.get_value<CustomDataType>()) {
        metas->customLayerData = v.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "customLayerData must be `dictionary` type, but got type `" +
            fv.second.type_name());
      }
    } else if (fv.first == "primChildren") {  // only appears in USDC.
      auto v = fv.second.get_value<std::vector<value::token>>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("Type must be `token[]` for `primChildren`, but got " +
                   fv.second.type_name());
      }

      metas->primChildren = v.value();
    } else if (fv.first == "documentation") {  // 'doc'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("Type must be `string` for `documentation`, but got " +
                   fv.second.type_name());
      }
      value::StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->doc = sdata;
      DCOUT("doc = " << metas->doc.value);
    } else if (fv.first == "comment") {  // 'comment'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("Type must be `string` for `comment`, but got " +
                   fv.second.type_name());
      }
      value::StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->comment = sdata;
      DCOUT("comment = " << metas->comment.value);
    } else {
      PUSH_WARN("[StageMeta] TODO: " + fv.first);
    }
  }

  if (subLayers.size()) {
    std::vector<SubLayer> dst;
    for (size_t i = 0; i < subLayers.size(); i++) {
      SubLayer s;
      s.assetPath = subLayers[i];
      dst.push_back(s);
    }

    if (subLayers.size() == subLayerOffsets.size()) {
      for (size_t i = 0; i < subLayerOffsets.size(); i++) {
        dst[i].layerOffset = subLayerOffsets[i];
      }
    }

    metas->subLayers = dst;

  } else if (subLayerOffsets.size()) {
    PUSH_WARN("Corrupted subLayer info? `subLayers` Fileld not found.");
  }

  return true;
}

std::unique_ptr<Prim> USDCReader::Impl::ReconstructPrimFromTypeName(
    const std::string &typeName, // TinyUSDZ's Prim type name
    const std::string &primTypeName, // USD's Prim typeName
    const std::string &prim_name,
    const crate::CrateReader::Node &node, const Specifier spec,
    const std::vector<value::token> &primChildren,
    const std::vector<value::token> &properties,
    const PathIndexToSpecIndexMap &psmap, const PrimMeta &meta, bool *is_unsupported_prim) {

  if (is_unsupported_prim) {
    (*is_unsupported_prim) = false; // init with false
  }


#define RECONSTRUCT_PRIM(__primty, __node_ty, __prim_name, __spec) \
  if (__node_ty == value::TypeTraits<__primty>::type_name()) {     \
    __primty typed_prim;                                           \
    if (!ReconstructPrim(__spec, node, psmap, &typed_prim)) {         \
      PUSH_ERROR("Failed to reconstruct Prim " << __node_ty << " elementName: " << __prim_name);      \
      return nullptr;                                              \
    }                                                              \
    typed_prim.meta = meta;                                        \
    typed_prim.name = __prim_name;                                 \
    typed_prim.spec = __spec;                                      \
    typed_prim.propertyNames() = properties; \
    typed_prim.primChildrenNames() = primChildren; \
    value::Value primdata(std::move(typed_prim));                            \
    auto result = std::unique_ptr<Prim>(new Prim(__prim_name, std::move(primdata)));  \
    result->prim_type_name() = primTypeName; \
    /* also add primChildren to Prim */ \
    result->metas().primChildren = primChildren; \
    return result; \
  } else

  if (typeName == "Model" || typeName == "__AnyType__") {
    // Code is mostly identical to RECONSTRUCT_PRIM.
    // Difference is store primTypeName to Model class itself.
    Model typed_prim;
    if (!ReconstructPrim(spec, node, psmap, &typed_prim)) {
      PUSH_ERROR("Failed to reconstruct Model");
      return nullptr;
    }
    typed_prim.meta = meta;
    typed_prim.name = prim_name;
    if (typeName == "__AnyType__") {
      typed_prim.prim_type_name = "";
    } else {
      typed_prim.prim_type_name = primTypeName;
    }
    typed_prim.spec = spec;
    typed_prim.propertyNames() = properties;
    typed_prim.primChildrenNames() = primChildren;
    value::Value primdata(std::move(typed_prim));
    auto result = std::unique_ptr<Prim>(new Prim(prim_name, std::move(primdata)));
    result->prim_type_name() = primTypeName;
    /* also add primChildren to Prim */
    result->metas().primChildren = primChildren;
    return result;
  } else

  RECONSTRUCT_PRIM(Xform, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Model, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Scope, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomMesh, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomPoints, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCylinder, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCube, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCone, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomSphere, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCapsule, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomBasisCurves, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomNurbsCurves, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomPointInstancer, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCamera, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomSubset, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(SphereLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DomeLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(CylinderLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DiskLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DistantLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(RectLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeometryLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(SkelRoot, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Skeleton, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(SkelAnimation, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(BlendShape, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Shader, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(NodeGraph, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Material, typeName, prim_name, spec) {
    PUSH_WARN("TODO or unsupported prim type: " << typeName);
    if (is_unsupported_prim) {
      (*is_unsupported_prim) = true;
    }
    return nullptr;
  }

#undef RECONSTRUCT_PRIM
}

///
///
/// Prim(Model) fieldSet example.
///
///
///   specTy = SpecTypePrim
///
///     - specifier(specifier) : e.g. `def`, `over`, ...
///     - kind(token) : kind metadataum
///     - optional: typeName(token) : type name of Prim(e.g. `Xform`). No
///     typeName = `def "mynode"`
///     - primChildren(TokenVector): List of child prims.
///     - properties(TokenVector) : List of name of Prim properties.
///
///
bool USDCReader::Impl::ParsePrimSpec(const crate::FieldValuePairVector &fvs,
                                     nonstd::optional<std::string> &typeName,
                                     nonstd::optional<Specifier> &specifier,
                                     std::vector<value::token> &primChildren,
                                     std::vector<value::token> &properties,
                                     PrimMeta &primMeta) {
  // Fields for Prim and Prim metas.
  for (const auto &fv : fvs) {
    if (fv.first == "typeName") {
      if (auto pv = fv.second.as<value::token>()) {
        typeName = pv->str();
        DCOUT("typeName = " << typeName.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`typeName` must be type `token`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "specifier") {
      if (auto pv = fv.second.as<Specifier>()) {
        specifier = (*pv);
        DCOUT("specifier = " << to_string(specifier.value()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`specifier` must be type `Specifier`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "properties") {
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        properties = (*pv);
        DCOUT("properties = " << properties);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`properties` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "primChildren") {
      // Crate only
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        primChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`primChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "active") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.set_active(*pv);
        DCOUT("active = " << to_string(primMeta.get_active()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`active` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "hidden") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.set_hidden(*pv);
        DCOUT("hidden = " << to_string(primMeta.get_hidden()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`hidden` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "instanceable") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.set_instanceable(*pv);
        DCOUT("instanceable = " << to_string(primMeta.get_instanceable()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`instanceable` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "assetInfo") {
      // CustomData(dict)
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_assetInfo(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`assetInfo` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "clips") {
      // CustomData(dict)
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_clips(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`clips` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "kind") {
      if (auto pv = fv.second.as<value::token>()) {

          const value::token tok = (*pv);
          if (tok.str() == "subcomponent") {
            primMeta.set_kind(Kind::Subcomponent);
          } else if (tok.str() == "component") {
            primMeta.set_kind(Kind::Component);
          } else if (tok.str() == "model") {
            primMeta.set_kind(Kind::Model);
          } else if (tok.str() == "group") {
            primMeta.set_kind(Kind::Group);
          } else if (tok.str() == "assembly") {
            primMeta.set_kind(Kind::Assembly);
          } else if (tok.str() == "sceneLibrary") {
            // USDZ specific: https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/scenelibrary
            primMeta.set_kind(Kind::SceneLibrary);
          } else {
            // For user-defined kind, store the string directly
            primMeta.set_kind(tok.str());
          }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`kind` must be type `token`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "apiSchemas") {
      if (auto pv = fv.second.as<ListOp<value::token>>()) {
        auto listop = (*pv);

        std::string warn;
        auto ret = ToAPISchemas(listop, _config.allow_unknown_apiSchemas, warn);
        if (!ret) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Failed to validate `apiSchemas`: " + ret.error());
        } else {
          if (warn.size()) {
            PUSH_WARN(warn);
          }
          primMeta.set_apiSchemas(*ret);
        }
        // DCOUT("apiSchemas = " << to_string(listop));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`apiSchemas` must be type `ListOp[Token]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "documentation") {
      if (auto pv = fv.second.as<std::string>()) {
        value::StringData s;
        // Crate UNREGISTERED_VALUE strings may contain literal quotes;
        // strip them so pprinter doesn't double-quote.
        s.value = unwrap(*pv);
        s.is_triple_quoted = hasNewline(s.value);
        primMeta.set_doc(s);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`documentation` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "comment") {
      if (auto pv = fv.second.as<std::string>()) {
        value::StringData s;
        s.value = unwrap(*pv);
        s.is_triple_quoted = hasNewline(s.value);
        primMeta.set_comment(s);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`comment` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "sdrMetadata") {
      // CustomData(dict)
      if (auto pv = fv.second.as<CustomDataType>()) {
        // TODO: Check if all keys are string type.
        primMeta.set_sdrMetadata(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`sdrMetadata` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "customData") {
      // CustomData(dict)
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_customData(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`customData` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "variantSelection") {
      if (auto pv = fv.second.as<VariantSelectionMap>()) {
        primMeta.variants = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantSelection` must be type `variants`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "variantChildren") {
      // Used internally
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        primMeta.variantChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "variantSetChildren") {
      // Used internally
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        primMeta.variantSetChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantSetChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "variantSetNames") {
      // ListOp<string>
      if (auto pv = fv.second.as<ListOp<std::string>>()) {
        const ListOp<std::string> &p = *pv;
        DCOUT("variantSetNames = " << to_string(p));

        auto ps = DecodeListOp<std::string>(p);

        // Store all listops (supports multiple listops per arc)
        primMeta.variantSets = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`variantSetNames` must be type `ListOp[String]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "sceneName") {  // USDZ extension
      if (auto pv = fv.second.as<std::string>()) {
        // Crate UNREGISTERED_VALUE strings may contain literal quotes;
        // strip them so pprinter's quote() doesn't double-quote.
        primMeta.set_sceneName(unwrap(*pv));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`sceneName` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "displayName") {  // USD supported since 23.xx?
      if (auto pv = fv.second.as<std::string>()) {
        primMeta.set_displayName(unwrap(*pv));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`displayName` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "inherits") {  // `inherits` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        (void)pvb;
        // make empty array
        primMeta.inherits = std::vector<std::pair<ListEditQual, std::vector<Path>>>();
        primMeta.inherits->push_back(
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Path>()));
      } else if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("inherits = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        // Store all listops (supports multiple listops per arc)
        primMeta.inherits = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`inherits` must be type `path` o `path[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "references") {  // `references` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        (void)pvb;
        // make empty array
        primMeta.references = std::vector<std::pair<ListEditQual, std::vector<Reference>>>();
        primMeta.references->push_back(
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Reference>()));
      } else if (auto pv = fv.second.as<ListOp<Reference>>()) {
        const ListOp<Reference> &p = *pv;
        DCOUT("references = " << to_string(p));

        auto ps = DecodeListOp<Reference>(p);

        // Store all listops (supports multiple listops per arc)
        primMeta.references = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`references` must be type `ListOp[Reference]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "payload") {  // `payload` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        (void)pvb;
        // make empty array
        primMeta.payload = std::vector<std::pair<ListEditQual, std::vector<Payload>>>();
        primMeta.payload->push_back(
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Payload>()));
      } else if (auto pv = fv.second.as<Payload>()) {
        // payload can be non-listop
        std::vector<Payload> pls;
        pls.push_back(*pv);
        primMeta.payload = std::vector<std::pair<ListEditQual, std::vector<Payload>>>();
        primMeta.payload->push_back(std::make_pair(ListEditQual::ResetToExplicit, pls));
      } else if (auto pvs = fv.second.as<ListOp<Payload>>()) {
        const ListOp<Payload> &p = *pvs;
        DCOUT("payload = " << to_string(p));

        auto ps = DecodeListOp<Payload>(p);

        // Store all listops (supports multiple listops per arc)
        primMeta.payload = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`payload` must be type `ListOp[Payload]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "specializes") {  // `specializes` composition
      if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("specializes = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        // Store all listops (supports multiple listops per arc)
        primMeta.specializes = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`specializes` must be type `ListOp[Path]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "inheritPaths") {  // `inherits` composition (alternate field name)
      if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("inheritPaths = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        // USDC uses "inheritPaths" field name but we store it in "inherits" for consistency
        // Store all listops (supports multiple listops per arc)
        primMeta.inherits = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`inheritPaths` must be type `ListOp[Path]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (endsWith(fv.first, ".targetPaths")) {
      // Handle relationship target paths (e.g., "myRel.targetPaths")
      // Store as unregistered metadata for now (full relationship support would require Prim changes)
      if (auto pv = fv.second.as<std::vector<Path>>()) {
        DCOUT("Relationship " << fv.first << " = " << to_string(*pv));
        primMeta.unregisteredMetas[fv.first] = to_string(*pv);
      } else {
        PUSH_WARN("Relationship targetPaths field `" << fv.first << "` is not Path vector type (got " << fv.second.type_name() << "). Ignoring.");
      }
    } else {
      // TODO: support int, int[], uint, uint[], int64, uint64, ...
      // https://github.com/syoyo/tinyusdz/issues/106
      if (auto pv = fv.second.as<std::string>()) {
        // Assume unregistered Prim metadatum
        primMeta.unregisteredMetas[fv.first] = (*pv);
      } else if (auto ptv = fv.second.as<value::token>()) {
        // store value as string type.
        primMeta.unregisteredMetas[fv.first] = quote((*ptv).str());
      } else {
        DCOUT("PrimProp TODO: " << fv.first);
        PUSH_WARN("PrimProp TODO: " << fv.first);
      }
    }
  }

  return true;
}

///
/// SpecTypeVariantSet seems only contain `variantChildren` field.
/// This info is used for the ordering of `variantSet` stmt?
///
///   specTy = SpecTypeVariantSet
///
///     - variantChildren(token[])
///
///
bool USDCReader::Impl::ParseVariantSetFields(
    const crate::FieldValuePairVector &fvs,
    std::vector<value::token> &variantChildren) {
  // Fields for Prim and Prim metas.
  for (const auto &fv : fvs) {
    if (fv.first == "variantChildren") {
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        variantChildren = (*pv);
        DCOUT("variantChildren: " << variantChildren);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else {
      DCOUT("Unknown/invalid field in VariantSet: " << fv.first);
      PUSH_WARN("Ignoreing unknown/invalid field in VariantSet: " << fv.first);
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructPrimNode(int parent, int current, int level,
                                           bool is_parent_variant,
                                           const PathIndexToSpecIndexMap &psmap,
                                           Stage *stage,
                                           std::unique_ptr<Prim> *primOut) {
  (void)level;
  const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];

  DCOUT(fmt::format("parent = {}, curent = {}, is_parent_variant = {}", parent, current, is_parent_variant));

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  //std::cout << pprint::Indent(uint32_t(level)) << "lv[" << level
  //          << "] node_index[" << current << "] " << node.GetLocalPath()
  //          << " ==\n";
  //std::cout << pprint::Indent(uint32_t(level)) << " childs = [";
  //for (size_t i = 0; i < node.GetChildren().size(); i++) {
  //  std::cout << node.GetChildren()[i];
  //  if (i != (node.GetChildren().size() - 1)) {
  //    std::cout << ", ";
  //  }
  //}
  //std::cout << "] (is_parent_variant = " << is_parent_variant << ")\n";
#endif

  if (!psmap.count(uint32_t(current))) {
    // No specifier assigned to this node.
    DCOUT("No specifier assigned to this node: " << current);
    return true;  // would be OK.
  }

  uint32_t spec_index = psmap.at(uint32_t(current));
  if (spec_index >= _specs->size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs->size()) + ")");
    return false;
  }

  const crate::Spec &spec = (*_specs)[spec_index];

  DCOUT(pprint::Indent(uint32_t(level))
        << "  specTy = " << to_string(spec.spec_type));
  DCOUT(pprint::Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if ((spec.spec_type == SpecType::Attribute) ||
      (spec.spec_type == SpecType::Relationship)) {
    if (_prim_table.count(parent)) {
      // This node is a Properties node. These are processed in
      // ReconstructPrim(), so nothing to do here.
      return true;
    }
  }

  crate::FieldValuePairVector decoded_fvs;
  const crate::FieldValuePairVector *fvs_ptr = nullptr;
  if (!ResolveFieldValuePairs(spec, &fvs_ptr, &decoded_fvs)) {
    return false;
  }
  const crate::FieldValuePairVector &fvs = (*fvs_ptr);

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
  // DBG
  for (auto &fv : fvs) {
    DCOUT("parent[" << current << "] level [" << level << "] fv name "
                    << fv.first << "(type = " << fv.second.type_name() << ")");
  }
#endif

  // StageMeta = root only attributes.
  // TODO: Unify reconstrction code with USDAReder?
  if (current == 0) {
    if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
      DCOUT("Root element path: " << pv.value().full_path_name());
    } else {
      PUSH_ERROR_AND_RETURN("(Internal error). Root Element Path not found.");
    }

    // Root layer(Stage) is PseudoRoot spec type.
    if (spec.spec_type != SpecType::PseudoRoot) {
      PUSH_ERROR_AND_RETURN(
          "SpecTypePseudoRoot expected for root layer(Stage) element.");
    }

    if (!ReconstrcutStageMeta(fvs, &stage->metas())) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct StageMeta.");
    }

    // TODO: Validate scene using `StageMetas::primChildren`.

    _prim_table.insert(current);

    return true;
  }

  DCOUT("spec.type = " << to_string(spec.spec_type));
  switch (spec.spec_type) {
    case SpecType::PseudoRoot: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "SpecType PseudoRoot in a child node is not supported(yet)");
    }
    case SpecType::Prim: {
      nonstd::optional<std::string> typeName;
      nonstd::optional<Specifier> specifier;
      std::vector<value::token> primChildren;
      std::vector<value::token> properties;

      PrimMeta primMeta;

      DCOUT("== PrimFields begin ==> ");

      if (!ParsePrimSpec(fvs, typeName, specifier, primChildren, properties, primMeta)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse Prim fields.");
        return false;
      }

      DCOUT("<== PrimFields end ===");

      Path elemPath;

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        DCOUT(fmt::format("Element path: {}", pv.value().full_path_name()));
        elemPath = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      // Sanity check
      if (specifier) {
        if (specifier.value() == Specifier::Def) {
          // ok
        } else if (specifier.value() == Specifier::Class) {
          // ok
        } else if (specifier.value() == Specifier::Over) {
          // ok
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
        }
      } else {
        // default `over`
        specifier = Specifier::Over;
      }

      std::string pTyName;
      if (!typeName) {
        //PUSH_WARN("Treat this node as Model(`typeName` field is missing).");
        pTyName = "Model";
      } else {
        pTyName = typeName.value();
      }

      {
        DCOUT("elemPath.prim_name = " << elemPath.prim_part());
        std::string prim_name = elemPath.prim_part();
        std::string primTypeName = typeName.has_value() ? typeName.value() : "";

        // __AnyType__
        if (typeName.has_value() && typeName.value() == "__AnyType__") {
          primTypeName = "";
        }

        // Validation check should be already done in crate-reader, so no
        // further validation required.
        if (!ValidatePrimElementName(prim_name)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Prim name.");
        }

        bool is_unsupported_prim{false};
        auto prim = ReconstructPrimFromTypeName(pTyName, primTypeName, prim_name,
                                                node, specifier.value(), primChildren, properties,
                                                psmap, primMeta, &is_unsupported_prim);

        // Fallback: try to reconstruct as Model if unknown prim type
        if (!prim && _config.allow_unknown_prims && is_unsupported_prim) {
          prim = ReconstructPrimFromTypeName("Model", primTypeName, prim_name,
                                             node, specifier.value(), primChildren, properties,
                                             psmap, primMeta);
        }

        if (!prim) {
          return false;
        }

        prim->element_path() = elemPath;

        // Move unique_ptr directly to output
        if (primOut) {
          *primOut = std::move(prim);
        }

      }

      DCOUT("add prim idx " << current);
      if (_prim_table.count(current)) {
        DCOUT("??? prim idx already set " << current);
      } else {
        _prim_table.insert(current);
      }

      break;
    }
    case SpecType::VariantSet: {
      DCOUT(
          fmt::format("[{}] is a VariantSet node(parent = {}). prim_idx? = {}",
                      current, parent, _prim_table.count(current)));

      Path elemPath;
      std::array<std::string, 2> toks;

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();

        DCOUT(fmt::format("Element path: {}", dump_path(elemPath)));

        // Ensure ElementPath is variant
        if (!tokenize_variantElement(elemPath.full_path_name(), &toks)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        if (toks[0].empty()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Empty variantSet name in element path.");
        }

        if (toks[1].size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Element path must not have variant.");
        }

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      // Assume parent(Prim or Variant(when nested)) already exists(parsed)
      // TODO: Confirm Crate format allow defining Prim after VariantSet
      // serialization.
      bool parent_is_variant = _variantPrims.count(parent) > 0;
      if (!_prim_table.count(parent) && !parent_is_variant) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  fmt::format("Parent Prim or Variant for the VariantSet not found. parent Prim/Variant = {}, isParentVariant = {}, VariantSet = {}", parent, parent_is_variant, elemPath.full_path_name()));
      }


      std::vector<value::token> variantChildren;

      // Only contains `variantChildren` field with type `token[]`

      DCOUT("== VariantSetFields begin ==> ");

      if (!ParseVariantSetFields(fvs, variantChildren)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse VariantSet fields.");
        return false;
      }

      DCOUT("<== VariantSetFields end === ");

      if (parent_is_variant) {
        // Nested variantSet inside a variant
        DCOUT("SpecTypeVariantSet: Add nested variantChildren(" << current << ") to parent Variant " << parent);
        if (!AddVariantChildrenToVariantNode(parent, current, /* variantSetName */toks[0], variantChildren)) {
          return false;
        }
      } else {
        DCOUT("SpecTypeVariantSet: Add variantChildren(" << current << ") to parent Prim " << parent);
        // Add variantChildren to prim node.
        // TODO: elemPath
        if (!AddVariantChildrenToPrimNode(parent, current, /* variantSetName */toks[0], variantChildren)) {
          return false;
        }
      }

      break;
    }
    case SpecType::Variant: {
      // Since the Prim this Variant node belongs to is not yet reconstructed
      // during the Prim tree traversal, we manage variant node separately

      DCOUT(fmt::format("[{}] is a Variant node(parent = {}). prim_idx? = {}",
                        current, parent, _prim_table.count(current)));

      nonstd::optional<std::string> typeName;
      nonstd::optional<Specifier> specifier;
      std::vector<value::token> primChildren;
      std::vector<value::token> properties;

      PrimMeta primMeta;

      DCOUT("== VariantFields begin ==> ");

      if (!ParsePrimSpec(fvs, typeName, specifier, primChildren, properties, primMeta)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Failed to parse Prim fields under Variant.");
        return false;
      }
      

      DCOUT("<== VariantFields end === ");

      Path elemPath;
      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();
        DCOUT(fmt::format("Element path: {}", elemPath.full_path_name()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      // Sanity check
      if (specifier) {
        if (specifier.value() == Specifier::Def) {
          // ok
        } else if (specifier.value() == Specifier::Class) {
          // ok
        } else if (specifier.value() == Specifier::Over) {
          // ok
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
        }
      } else {
        // Seems Variant is only composed of Properties.
        // Create pseudo `def` Prim
        specifier = Specifier::Def;
      }

      std::string pTyName; // TinyUSDZ' prim typename
      if (!typeName) {
        //PUSH_WARN("Treat this node as Model(where `typeName` is missing).");
        pTyName = "Model";
      } else {
        pTyName = typeName.value();
      }

      std::unique_ptr<Prim> variantPrim;
      {
        std::string prim_name = elemPath.prim_part();
        DCOUT("elemPath = " << dump_path(elemPath));
        DCOUT("prim_name = " << prim_name);
        if (primMeta.variantSets) {
          DCOUT("primMeta.variantSets = " << primMeta.variantSets.value().second);
        }

        std::string primTypeName = typeName.has_value() ? typeName.value() : "";
        // __AnyType__
        if (typeName.has_value() && typeName.value() == "__AnyType__") {
          primTypeName = "";
        }

        // Something like '{shapeVariant=Capsule}'

        std::array<std::string, 2> variantPair;
        if (!tokenize_variantElement(prim_name, &variantPair)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        std::string variantSetName = variantPair[0];
        std::string variantPrimName = variantPair[1];

        if (!ValidatePrimElementName(variantPrimName)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Prim name in Variant: `{}`",
                                variantPrimName));
        }

        // Fix: Transfer parsed primChildren to primMeta.primChildren
        primMeta.primChildren = primChildren;


        // Helper lambda to setup and store variant prim
        auto setupAndStoreVariantPrim = [&](std::unique_ptr<Prim>& vp) {
          if (vp->metas().variantSets) {
            DCOUT("variantPrim.meta.variantSets = " << vp->metas().variantSets.value().second);
          }
          vp->element_path() = elemPath;
          vp->specifier() = specifier.value();

          // Store variantPrim to temporary buffer
          DCOUT(fmt::format("parent {} add prim idx {} as variant: ", parent, current));
          if (_variantPrims.count(current)) {
            DCOUT("??? prim idx already set " << current);
          } else {
            _variantPrims.emplace(current, std::move(*vp));
            _variantPrimChildren[parent].push_back(current);
            // Add to name index for O(1) lookup
            _variantNameIndex[{variantSetName, variantPrimName}] = current;
          }
        };

        bool is_unsupported_prim{false};
        variantPrim = ReconstructPrimFromTypeName(
            pTyName, primTypeName, variantPrimName, node, specifier.value(), primChildren, properties,
            psmap, primMeta, &is_unsupported_prim);

        if (variantPrim) {
          setupAndStoreVariantPrim(variantPrim);
        } else if (_config.allow_unknown_prims && is_unsupported_prim) {
          // Fallback: try to reconstruct as Model
          variantPrim = ReconstructPrimFromTypeName(
              "Model", primTypeName, variantPrimName, node, specifier.value(), primChildren, properties,
              psmap, primMeta);

          if (variantPrim) {
            setupAndStoreVariantPrim(variantPrim);
          } else {
            return false;
          }
        } else {
          return false;
        }
      }

      // Note: primOut not set for variant prims since they're stored in _variantPrims
      // and accessed separately. The caller handles this via is_parent_variant checks.

      break;
    }
    case SpecType::Attribute: {
      if (is_parent_variant) {
        Path path;
        if (!GetPathDirect(spec.path_index, &path)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
        }

        Property prop;
        if (!ParseProperty(spec.spec_type,
                           const_cast<crate::FieldValuePairVector &>(fvs),
                           &prop, _config.use_lazy_property_construction)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    fmt::format("Failed to parse Attribute: {}.",
                                                path.prop_part()));
        }

        // Parent Prim is not yet reconstructed, so store info to temporary
        // buffer _variantAttributeNodes.
        _variantProps[current] = {path, prop};
        _variantPropChildren[parent].push_back(current);

        DCOUT(
            fmt::format("parent {} current [{}] Parsed Property/Attribute {} under Variant. PathIndex {}",
                        parent, current, path.prop_part(), spec.path_index));

      } else {
        // Maybe parent is Class/Over, or inherited
        PUSH_WARN(
            "TODO: SpecTypeAttribute(in conjunction with Class/Over specifier, "
            "or inherited?)");
      }
      break;
    }
    case SpecType::Connection:
    case SpecType::Relationship:
    case SpecType::RelationshipTarget: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, fmt::format("TODO: Unsupported/Unimplemented SpecType: {}.",
                            to_string(spec.spec_type)));
      break;
    }
    case SpecType::Expression:
    case SpecType::Mapper:
    case SpecType::MapperArg: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Unsupported SpecType: {}.",
                                                  to_string(spec.spec_type)));
      break;
    }
    case SpecType::Unknown:
    case SpecType::Invalid: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] Invalid SpecType.");
      break;
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructPrimSpecNode(int parent, int current, int level,
                                           bool is_parent_variant,
                                           const PathIndexToSpecIndexMap &psmap,
                                           Layer *layer,
                                           PrimSpec *primOut) {
  (void)level;
  const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  //std::cout << pprint::Indent(uint32_t(level)) << "lv[" << level
  //          << "] node_index[" << current << "] " << node.GetLocalPath()
  //          << " ==\n";
  //std::cout << pprint::Indent(uint32_t(level)) << " childs = [";
  //for (size_t i = 0; i < node.GetChildren().size(); i++) {
  //  std::cout << node.GetChildren()[i];
  //  if (i != (node.GetChildren().size() - 1)) {
  //    std::cout << ", ";
  //  }
  //}
  //std::cout << "] (is_parent_variant = " << is_parent_variant << ")\n";
#endif

  if (!psmap.count(uint32_t(current))) {
    // No specifier assigned to this node.
    DCOUT("No specifier assigned to this node: " << current);
    return true;  // would be OK.
  }

  uint32_t spec_index = psmap.at(uint32_t(current));
  if (spec_index >= _specs->size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs->size()) + ")");
    return false;
  }

  const crate::Spec &spec = (*_specs)[spec_index];

  DCOUT(pprint::Indent(uint32_t(level))
        << "  specTy = " << to_string(spec.spec_type));
  DCOUT(pprint::Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if ((spec.spec_type == SpecType::Attribute) ||
      (spec.spec_type == SpecType::Relationship)) {
    if (_prim_table.count(parent)) {
      // This node is a Properties node. These are processed in
      // ReconstructPrim(), so nothing to do here.
      return true;
    }
  }

  crate::FieldValuePairVector decoded_fvs;
  const crate::FieldValuePairVector *fvs_ptr = nullptr;
  if (!ResolveFieldValuePairs(spec, &fvs_ptr, &decoded_fvs)) {
    return false;
  }
  const crate::FieldValuePairVector &fvs = (*fvs_ptr);

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
  // DBG
  for (auto &fv : fvs) {
    DCOUT("parent[" << current << "] level [" << level << "] fv name "
                    << fv.first << "(type = " << fv.second.type_name() << ")");
  }
#endif

  // StageMeta = root only attributes.
  // TODO: Unify reconstrction code with USDAReder?
  if (current == 0) {
    if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
      DCOUT("Root element path: " << pv.value().full_path_name());
    } else {
      PUSH_ERROR_AND_RETURN("(Internal error). Root Element Path not found.");
    }

    // Root layer(Stage) is PseudoRoot spec type.
    if (spec.spec_type != SpecType::PseudoRoot) {
      PUSH_ERROR_AND_RETURN(
          "SpecTypePseudoRoot expected for root layer(Stage) element.");
    }

    if (!ReconstrcutStageMeta(fvs, &layer->metas())) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct StageMeta.");
    }

    // TODO: Validate scene using `StageMetas::primChildren`.

    _prim_table.insert(current);

    return true;
  }

  switch (spec.spec_type) {
    case SpecType::PseudoRoot: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "SpecType PseudoRoot in a child node is not supported(yet)");
    }
    case SpecType::Prim: {
      nonstd::optional<std::string> typeName;
      nonstd::optional<Specifier> specifier;
      std::vector<value::token> primChildren;
      std::vector<value::token> properties;

      PrimMeta primMeta;

      DCOUT("== PrimFields begin ==> ");

      if (!ParsePrimSpec(fvs, typeName, specifier, primChildren, properties, primMeta)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse Prim fields.");
        return false;
      }

      DCOUT("<== PrimFields end ===");

      Path elemPath;

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        DCOUT(fmt::format("Element path: {}", pv.value().full_path_name()));
        elemPath = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      // Sanity check
      if (specifier) {
        if (specifier.value() == Specifier::Def) {
          // ok
        } else if (specifier.value() == Specifier::Class) {
          // ok
        } else if (specifier.value() == Specifier::Over) {
          // ok
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
        }
      } else {
        // Default = Over Prim.
        specifier = Specifier::Over;
      }

      std::string pTyName;
      if (!typeName) {
        //PUSH_WARN("Treat this node as Model(`typeName` field is missing).");
        pTyName = "Model";
      } else {
        pTyName = typeName.value();
      }

      {
        DCOUT("elemPath.prim_name = " << elemPath.prim_part());
        std::string prim_name = elemPath.prim_part();
        std::string primTypeName = typeName.has_value() ? typeName.value() : "";
        // __AnyType__
        if (typeName.has_value() && typeName.value() == "__AnyType__") {
          primTypeName = "";
        }

        // Validation check should be already done in crate-reader, so no
        // further validation required.
        if (!ValidatePrimElementName(prim_name)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Prim name.");
        }

        PrimSpec primspec;

        primspec.typeName() = primTypeName;
        primspec.name() = prim_name;

        prim::PropertyMap props;
        if (!BuildPropertyMap(node.GetChildren(), psmap, &props)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
        }
        primspec.props() = std::move(props);
        primspec.metas() = std::move(primMeta);

        if (primOut) {
          (*primOut) = std::move(primspec);
        }
      }

      DCOUT("add prim idx " << current);
      if (_prim_table.count(current)) {
        DCOUT("??? prim idx already set " << current);
      } else {
        _prim_table.insert(current);
      }

      break;
    }
    case SpecType::VariantSet: {

      // VariantSet spec contains the info of variantChildren,
      // and added to parent Prim or Variant(when Variant is nested)
      // No child node would exist for VariantSet spec.
      //
      // Assume parent(Prim) already exists(parsed)
      // TODO: Confirm Crate format whether it allows defining Prim spec after VariantSet spec
      // serialization.
      if (!_prim_table.count(parent)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Parent Prim for this VariantSet not found.");
      }

      DCOUT(
          fmt::format("[{}] is a VariantSet node(parent = {}). prim_idx? = {}",
                      current, parent, _prim_table.count(current)));

      Path elemPath;
      std::array<std::string, 2> toks;

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();

        DCOUT(fmt::format("Element path: {}", dump_path(elemPath)));

        // Ensure ElementPath is variant
        if (!tokenize_variantElement(elemPath.full_path_name(), &toks)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        if (toks[0].empty()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Invalid Element path.");
        }

        if (toks[1].size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Invalid Element path.");
        }

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      std::vector<value::token> variantChildren;

      DCOUT("== VariantSetFields begin ==> ");

      if (!ParseVariantSetFields(fvs, variantChildren)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse VariantSet fields.");
        return false;
      }

      DCOUT("<== VariantSetFields end === ");

      DCOUT("Add variantChildren(" << current << ") to parent Prim " << parent);

      // Add variantChildren to prim node.
      // TODO: elemPath
      if (!AddVariantChildrenToPrimNode(parent, current, /* variantSetName */toks[0], variantChildren)) {
        return false;
      }

      break;
    }
    case SpecType::Variant: {
      // Since the (parent) Prim this Variant node belongs to is not yet reconstructed
      // during the Prim tree traversal, We manage variant node separately
      

      // TODO: Check if corresponding VariantSet Spec(which contains the list of VariantSetNames)
      // is defined a priori.

      DCOUT(fmt::format("[{}] is a Variant node(parent = {}). prim_idx? = {}",
                        current, parent, _prim_table.count(current)));

      nonstd::optional<std::string> typeName;
      nonstd::optional<Specifier> specifier;
      std::vector<value::token> primChildren;
      std::vector<value::token> properties;

      PrimMeta primMeta;

      DCOUT("== VariantFields begin ==> ");

      if (!ParsePrimSpec(fvs, typeName, specifier, primChildren, properties, primMeta)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Failed to parse Prim fields under Variant.");
        return false;
      }

      DCOUT("<== VariantFields end === ");

      Path elemPath;
      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();
        DCOUT(fmt::format("Element path: {}", elemPath.full_path_name()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      // Sanity check
      if (specifier) {
        if (specifier.value() == Specifier::Def) {
          // ok
        } else if (specifier.value() == Specifier::Class) {
          // ok
        } else if (specifier.value() == Specifier::Over) {
          // ok
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
        }
      } else {
        // Seems Variant is only composed of Properties.
        // Create pseudo `def` Prim
        // FIXME: default is `Over`?
        specifier = Specifier::Def;
      }

      std::string pTyName; // TinyUSDZ' prim typename
      if (!typeName) {
        //PUSH_WARN("Treat this node as Model(where `typeName` is missing).");
        pTyName = "Model";
      } else {
        pTyName = typeName.value();
      }

      {
        std::string prim_name = elemPath.prim_part();
        DCOUT("elemPath = " << dump_path(elemPath));
        DCOUT("prim_name = " << prim_name);

        std::string primTypeName = typeName.has_value() ? typeName.value() : "";
        // __AnyType__
        if (typeName.has_value() && typeName.value() == "__AnyType__") {
          primTypeName = "";
        }

        // Something like '{shapeVariant=Capsule}'

        std::array<std::string, 2> variantPair;
        if (!tokenize_variantElement(prim_name, &variantPair)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        std::string variantSetName = variantPair[0];
        std::string variantPrimName = variantPair[1];

        if (!ValidatePrimElementName(variantPrimName)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Prim name in Variant: `{}`",
                                variantPrimName));
        }

        PrimSpec variantPrimSpec;
        variantPrimSpec.typeName() = primTypeName;
        variantPrimSpec.name() = prim_name;

        prim::PropertyMap props;
        if (!BuildPropertyMap(node.GetChildren(), psmap, &props)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
        }
        variantPrimSpec.props() = std::move(props);
        variantPrimSpec.metas() = std::move(primMeta);

        // Store variantPrimSpec to temporary buffer.
        DCOUT(fmt::format("parent {} add primspec idx {} as variant: ", parent, current));
        if (_variantPrimSpecs.count(current)) {
          DCOUT("??? prim idx already set " << current);
        } else {
          _variantPrimSpecs[current] = std::move(variantPrimSpec);
          _variantPrimChildren[parent].push_back(current);
        }
      }

      break;
    }
    case SpecType::Attribute: {
      if (is_parent_variant) {
        Path path;
        if (!GetPathDirect(spec.path_index, &path)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
        }

        Property prop;
        if (!ParseProperty(spec.spec_type,
                           const_cast<crate::FieldValuePairVector &>(fvs),
                           &prop, _config.use_lazy_property_construction)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    fmt::format("Failed to parse Attribute: {}.",
                                                path.prop_part()));
        }

        // Parent Prim is not yet reconstructed, so store info to temporary
        // buffer _variantAttributeNodes.
        _variantProps[current] = {path, prop};
        _variantPropChildren[parent].push_back(current);

        DCOUT(
            fmt::format("parent {} current [{}] Parsed Property/Attribute {} under Variant. PathIndex {}",
                        parent, current, path.prop_part(), spec.path_index));

      } else {
        // Maybe parent is Class/Over, or inherited
        PUSH_WARN(
            "TODO: SpecTypeAttribute(in conjunction with Class/Over specifier, "
            "or inherited?)");
      }
      break;
    }
    case SpecType::Connection:
    case SpecType::Relationship:
    case SpecType::RelationshipTarget: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, fmt::format("TODO: Unsupported/Unimplemented SpecType: {}.",
                            to_string(spec.spec_type)));
      break;
    }
    case SpecType::Expression:
    case SpecType::Mapper:
    case SpecType::MapperArg: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Unsupported SpecType: {}.",
                                                  to_string(spec.spec_type)));
      break;
    }
    case SpecType::Unknown:
    case SpecType::Invalid: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] Invalid SpecType.");
      break;
    }
  }

  return true;
}

// Switch between recursive and iterative implementation
// Set to 1 to use iterative implementation, 0 to use original recursive implementation
#define TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM 1

#if TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM

//
// Iterative version of ReconstructPrimRecursively using explicit stack
// This avoids stack overflow for deeply nested prim hierarchies
//
// Uses std::unique_ptr<Prim> instead of nonstd::optional<Prim> for move-friendly
// semantics. unique_ptr has noexcept move constructor, so vector reallocation
// will move (not copy) StackEntry objects efficiently.
//
bool USDCReader::Impl::ReconstructPrimRecursively(
    int parent, int current, Prim *parentPrim, int level,
    const PathIndexToSpecIndexMap &psmap, Stage *stage) {

  // parentPrim is not used in iterative version - we track via parent_entry_idx
  (void)parentPrim;

  // Stack entry for iterative processing
  // We use indices to parent entries to maintain parent-child relationships
  // Using unique_ptr<Prim> for move-only semantics (no Prim copies)
  struct StackEntry {
    int parent_id;           // Parent node id
    int current_id;          // Current node id
    int level;               // Nesting level
    size_t child_idx;        // Which child we're processing next
    size_t parent_entry_idx; // Index of parent entry in stack (SIZE_MAX for none)
    std::unique_ptr<Prim> prim;  // Reconstructed prim for this node (nullptr if none)

    StackEntry(int p, int c, int lv, size_t parent_idx)
        : parent_id(p), current_id(c), level(lv), child_idx(0),
          parent_entry_idx(parent_idx), prim(nullptr) {}

    // Move-only
    StackEntry(StackEntry &&) noexcept = default;
    StackEntry &operator=(StackEntry &&) noexcept = default;
    StackEntry(const StackEntry &) = delete;
    StackEntry &operator=(const StackEntry &) = delete;
  };

  // Use vector as stack - reserve space to minimize reallocations
  std::vector<StackEntry> stack;
  stack.reserve(size_t(_config.kMaxPrimNestLevel) + 16);

  // Push initial entry
  stack.emplace_back(parent, current, level, SIZE_MAX);

  while (!stack.empty()) {
    StackEntry &entry = stack.back();

    // Validate current node id
    if ((entry.current_id < 0) || (entry.current_id >= int(_nodes->size()))) {
      PUSH_ERROR("Invalid current node id: " + std::to_string(entry.current_id) +
                 ". Must be in range [0, " + std::to_string(_nodes->size()) + ")");
      return false;
    }

    // Check nesting level
    if (entry.level > int32_t(_config.kMaxPrimNestLevel)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Prim hierarchy is too deep.");
    }

    const crate::CrateReader::Node &node = (*_nodes)[size_t(entry.current_id)];
    const auto &children = node.GetChildren();

    // First time visiting this node - reconstruct prim
    if (entry.child_idx == 0 && !entry.prim) {
      DCOUT("ReconstructPrimRecursively: parent = "
            << std::to_string(entry.parent_id) << ", current = " << entry.current_id
            << ", level = " << std::to_string(entry.level));

      bool is_parent_variant = _variantPrims.count(entry.parent_id);

      std::unique_ptr<Prim> temp_prim;
      if (!ReconstructPrimNode(entry.parent_id, entry.current_id, entry.level,
                               is_parent_variant, psmap, stage, &temp_prim)) {
        return false;
      }
      // Direct move of unique_ptr (no intermediate optional, no copy)
      if (temp_prim) {
        entry.prim = std::move(temp_prim);
      }

      DCOUT("node.Children.size = " << children.size());
    }

    // Process children
    if (entry.child_idx < children.size()) {
      size_t idx = entry.child_idx++;
      int child_id = int(children[idx]);

      DCOUT("Reconstuct Prim children: " << idx << " / " << children.size());

      // Get current stack size as parent index for the new entry
      size_t current_entry_idx = stack.size() - 1;

      // Push child entry
      stack.emplace_back(entry.current_id, child_id, entry.level + 1, current_entry_idx);
    } else {
      // All children processed - finalize this node
      DCOUT("DONE processing children for node: " << entry.current_id);

      //
      // Reconstruct variant
      //
      DCOUT(fmt::format("parent {}, current {}", entry.parent_id, entry.current_id));

      DCOUT(fmt::format("  has variant properties {}, has variant children {}",
        _variantPropChildren.count(entry.current_id),
        _variantPrimChildren.count(entry.current_id)));

      // Get parent prim pointer
      Prim *parentPrimPtr = nullptr;
      if (entry.parent_entry_idx != SIZE_MAX && entry.parent_entry_idx < stack.size()) {
        parentPrimPtr = stack[entry.parent_entry_idx].prim.get();
      }

      auto resolveVariantOwnerPrimFromStack = [&](const StackEntry &target,
                                                  bool include_current) -> Prim * {
        if (include_current) {
          if (target.prim) {
            return target.prim.get();
          }

          auto current_variant_it = _variantPrims.find(target.current_id);
          if (current_variant_it != _variantPrims.end()) {
            return &(current_variant_it->second);
          }
        }

        size_t ancestor_idx = target.parent_entry_idx;
        while ((ancestor_idx != SIZE_MAX) && (ancestor_idx < stack.size())) {
          StackEntry &ancestor = stack[ancestor_idx];
          if (ancestor.prim) {
            return ancestor.prim.get();
          }

          auto ancestor_variant_it = _variantPrims.find(ancestor.current_id);
          if (ancestor_variant_it != _variantPrims.end()) {
            return &(ancestor_variant_it->second);
          }

          ancestor_idx = ancestor.parent_entry_idx;
        }

        auto parent_variant_it = _variantPrims.find(target.parent_id);
        if (parent_variant_it != _variantPrims.end()) {
          return &(parent_variant_it->second);
        }

        if (!include_current) {
          if (target.prim) {
            return target.prim.get();
          }

          auto current_variant_it = _variantPrims.find(target.current_id);
          if (current_variant_it != _variantPrims.end()) {
            return &(current_variant_it->second);
          }
        }

        return nullptr;
      };

      if (_variantPropChildren.count(entry.current_id)) {

        // - parentPrim
        //   - variantPrim(SpecTypeVariant) <- current
        //     - variant property(SpecTypeAttribute)

        //
        // `current` must be VariantPrim and `parentPrim` should exist
        //
        if (!_variantPrims.count(entry.current_id)) {
          PUSH_ERROR_AND_RETURN("Internal error: variant attribute is not a child of VariantPrim.");
        }

        Prim *variantOwnerPrim =
            resolveVariantOwnerPrimFromStack(entry, /* include_current */ false);
        if (!variantOwnerPrim) {
          PUSH_ERROR_AND_RETURN(
              "Internal error: parentPrim should exist for variant Property.");
        }

        const Prim &variantPrim = _variantPrims.at(entry.current_id);

        DCOUT("variant prim name: " << variantPrim.element_name());

        // element_name must be variant: "{variant=value}"
        if (!is_variantElementName(variantPrim.element_name())) {
          PUSH_ERROR_AND_RETURN("Corrupted Crate. VariantAttribute is not the child of VariantPrim.");
        }

        std::array<std::string, 2> toks;
        if (!tokenize_variantElement(variantPrim.element_name(), &toks)) {
          PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
        }

        std::string variantSetName = toks[0];
        std::string variantName = toks[1];

        Variant variant;

        for (const auto &item : _variantPropChildren.at(entry.current_id)) {
          // item should exist in _variantProps.
          if (!_variantProps.count(item)) {
            PUSH_ERROR_AND_RETURN("Internal error: variant Property not found.");
          }
          const std::pair<Path, Property> &pp = _variantProps.at(item);

          std::string prop_name = std::get<0>(pp).prop_part();
          DCOUT(fmt::format("  node_index = {}, prop name {}", item, prop_name));

          variant.properties()[prop_name] = std::get<1>(pp);
        }

        VariantSet &vs = variantOwnerPrim->variantSets()[variantSetName];

        if (vs.name.empty()) {
          vs.name = variantSetName;
        }
        vs.variantSet[variantName] = variant;

      }

      if (_variantPrimChildren.count(entry.current_id)) {
        Prim *variantOwnerPrim =
            resolveVariantOwnerPrimFromStack(entry, /* include_current */ true);
        if (!variantOwnerPrim) {
          PUSH_ERROR_AND_RETURN(
              "Internal error: failed to resolve variant owner Prim.");
        }

        if (!AttachVariantPrimChildrenToOwner(entry.current_id, variantOwnerPrim)) {
          return false;
        }
      }

      // Add prim to parent or root_prims (move out of unique_ptr)
      // Use resize + move assignment to avoid Prim copy (Prim now has default ctor)
      if (entry.parent_id == 0) {  // root prim
        if (entry.prim) {
          auto &prims = stage->root_prims();
          prims.resize(prims.size() + 1);
          prims.back() = std::move(*entry.prim);
        }
      } else {
        if (_variantPrims.count(entry.parent_id)) {
          // Add to variantPrim
          DCOUT("parent is variantPrim: " << entry.parent_id);
          if (!entry.prim) {
            if (!IsExpectedNonPrimVariantChild(entry.current_id, psmap)) {
              PUSH_WARN("parent is variantPrim, but current is not Prim.");
            }
          } else {
            DCOUT("Adding prim to child...");
            Prim &vp = _variantPrims.at(entry.parent_id);
            auto &vp_children = vp.children();
            vp_children.resize(vp_children.size() + 1);
            vp_children.back() = std::move(*entry.prim);
          }
        } else if (entry.prim && parentPrimPtr) {
          // Add to parent prim.
          auto &parent_children = parentPrimPtr->children();
          parent_children.resize(parent_children.size() + 1);
          parent_children.back() = std::move(*entry.prim);
        }
      }

      // Pop this entry
      stack.pop_back();
    }
  }

  return true;
}

#else // !TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM

//
// Original recursive implementation
// TODO: rewrite code in bottom-up manner
//
bool USDCReader::Impl::ReconstructPrimRecursively(
    int parent, int current, /* input */Prim *parentPrimPtr, int level,
    const PathIndexToSpecIndexMap &psmap, Stage *stage) {
  if (level > int32_t(_config.kMaxPrimNestLevel)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Prim hierarchy is too deep.");
  }

  DCOUT("ReconstructPrimRecursively: parent = "
        << std::to_string(parent) << ", current = " << current
        << ", level = " << std::to_string(level));

  if ((current < 0) || (current >= int(_nodes->size()))) {
    PUSH_ERROR("Invalid current node id: " + std::to_string(current) +
               ". Must be in range [0, " + std::to_string(_nodes->size()) + ")");
    return false;
  }

  Prim *currPrimPtr = nullptr;
  std::unique_ptr<Prim> prim;

  bool is_parent_variant = _variantPrims.count(parent);
  if (!ReconstructPrimNode(parent, current, level, is_parent_variant, psmap,
                           stage, &prim)) {
    return false;
  }

  if (prim) {
    currPrimPtr = prim.get();
  } else {
    auto it = _variantPrims.find(current);
    if (it != _variantPrims.end()) {
      currPrimPtr = &(it->second);
    }
  }

  Prim *nextParentPrimPtr = currPrimPtr ? currPrimPtr : parentPrimPtr;

  // Traverse children
  {
    const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];
    DCOUT("node.Children.size = " << node.GetChildren().size());
    for (size_t i = 0; i < node.GetChildren().size(); i++) {
      DCOUT("Reconstuct Prim children: " << i << " / "
                                         << node.GetChildren().size());
      if (!ReconstructPrimRecursively(current, int(node.GetChildren()[i]),
                                      nextParentPrimPtr, level + 1, psmap,
                                      stage)) {
        return false;
      }
      DCOUT("DONE Reconstuct Prim children: " << i << " / "
                                              << node.GetChildren().size());
    }
  }

  DCOUT(fmt::format("----reconstruct-variant------"));
  DCOUT(fmt::format("parent {}, current {}", parent, current));
  DCOUT(fmt::format("  has variant properties {}, has variant children {}",
                    _variantPropChildren.count(current),
                    _variantPrimChildren.count(current)));

  if (_variantPropChildren.count(current)) {
    if (!_variantPrims.count(current)) {
      PUSH_ERROR_AND_RETURN(
          "Internal error: variant attribute is not a child of VariantPrim.");
    }

    Prim *variantOwnerPrim =
        ResolveVariantOwnerPrim(parent, current, currPrimPtr, parentPrimPtr,
                                /* include_current_node */ false);
    if (!variantOwnerPrim) {
      PUSH_ERROR_AND_RETURN(
          "Internal error: parentPrim should exist for variant Property.");
    }

    const Prim &variantPrim = _variantPrims.at(current);
    DCOUT("variant prim name: " << variantPrim.element_name());

    // element_name must be variant: "{variant=value}"
    if (!is_variantElementName(variantPrim.element_name())) {
      PUSH_ERROR_AND_RETURN(
          "Corrupted Crate. VariantAttribute is not the child of VariantPrim.");
    }

    std::array<std::string, 2> toks;
    if (!tokenize_variantElement(variantPrim.element_name(), &toks)) {
      PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
    }

    std::string variantSetName = toks[0];
    std::string variantName = toks[1];

    Variant variant;
    for (const auto &item : _variantPropChildren.at(current)) {
      // item should exist in _variantProps.
      if (!_variantProps.count(item)) {
        PUSH_ERROR_AND_RETURN("Internal error: variant Property not found.");
      }

      const std::pair<Path, Property> &pp = _variantProps.at(item);
      std::string prop_name = std::get<0>(pp).prop_part();
      DCOUT(fmt::format("  node_index = {}, prop name {}", item, prop_name));
      variant.properties()[prop_name] = std::get<1>(pp);
    }

    VariantSet &vs = variantOwnerPrim->variantSets()[variantSetName];
    if (vs.name.empty()) {
      vs.name = variantSetName;
    }
    vs.variantSet[variantName] = variant;
  }

  if (_variantPrimChildren.count(current)) {
    Prim *variantOwnerPrim =
        ResolveVariantOwnerPrim(parent, current, currPrimPtr, parentPrimPtr,
                                /* include_current_node */ true);
    if (!variantOwnerPrim) {
      PUSH_ERROR_AND_RETURN("Internal error: failed to resolve variant owner Prim.");
    }

    if (!AttachVariantPrimChildrenToOwner(current, variantOwnerPrim)) {
      return false;
    }
  }

  DCOUT("-<---");

  // Use resize + move assignment to avoid Prim copy (Prim now has default ctor)
  if (parent == 0) {  // root prim
    if (prim) {
      auto &prims = stage->root_prims();
      prims.resize(prims.size() + 1);
      prims.back() = std::move(*prim);
    }
  } else {
    DCOUT("current " << current << ", parent " << parent);
    if (is_parent_variant) {
      // Add to variantPrim
      DCOUT("parent " << parent << " is variantPrim");
      if (!prim) {
        if (!IsExpectedNonPrimVariantChild(current, psmap)) {
          PUSH_WARN("parent is variantPrim, but current is not Prim.");
        }
      } else {
        DCOUT("Adding prim to child...");
        Prim &vp = _variantPrims.at(parent);
        auto &children = vp.children();
        children.resize(children.size() + 1);
        children.back() = std::move(*prim);
      }
    } else if (prim && parentPrimPtr) {
      // Add to parent prim.
      auto &children = parentPrimPtr->children();
      children.resize(children.size() + 1);
      children.back() = std::move(*prim);
    }
  }

  return true;
}

#endif // TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM

bool USDCReader::Impl::ReconstructStage(Stage *stage) {

  // Report progress (90% - starting reconstruction)
  if (_progress_callback) {
    if (!_progress_callback(0.9f, _progress_userptr)) {
      PUSH_ERROR("Reconstruction cancelled by progress callback.");
      return false;
    }
  }

  // format test
  DCOUT(fmt::format("# of Paths = {}", crate_reader->NumPaths()));

  if (crate_reader->NumNodes() == 0) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  // Use references to avoid copying data from crate_reader
  _nodes = &crate_reader->GetNodes();
  _specs = &crate_reader->GetSpecs();
  _fields = &crate_reader->GetFields();
  _fieldset_indices = &crate_reader->GetFieldsetIndices();
  _paths = &crate_reader->GetPaths();
  _elemPaths = &crate_reader->GetElemPaths();
  _live_fieldsets = &crate_reader->GetLiveFieldSets();

  PathIndexToSpecIndexMap
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs->size(); i++) {
      if ((*_specs)[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      if (path_index_to_spec_index_map.count((*_specs)[i].path_index.value) != 0) {
        PUSH_ERROR_AND_RETURN("Multiple PathIndex found in Crate data.");
      }

      DCOUT(fmt::format("path index[{}] -> spec index [{}]",
                        (*_specs)[i].path_index.value, uint32_t(i)));
      path_index_to_spec_index_map[(*_specs)[i].path_index.value] = uint32_t(i);
    }
  }

  stage->root_prims().clear();

  int root_node_id = 0;
  bool ret = ReconstructPrimRecursively(/* no further root for root_node */ -1,
                                        root_node_id, /* root Prim */ nullptr,
                                        /* level */ 0,
                                        path_index_to_spec_index_map, stage);

  if (!ret) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Stage(Prim hierarchy)");
  }

  stage->compute_absolute_prim_path_and_assign_prim_id();

  // Attach mmap array table to Stage for zero-copy access
  if (_config.mmap_zero_copy && !_mmap_table.empty()) {
    stage->set_mmap_table(std::move(_mmap_table));
  }

  // Free decompression buffers after reconstruction completes.
  // In lazy mode, decompression happens during reconstruction, so this is
  // the earliest safe point.
  crate_reader->ShrinkDecompressionBuffers();

  return true;
}

bool USDCReader::Impl::ReconstructPrimSpecRecursively(
    int parent, int current, PrimSpec *parentPrimSpec, int level,
    const PathIndexToSpecIndexMap &psmap, Layer *layer) {
  if (level > int32_t(_config.kMaxPrimNestLevel)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "PrimSpec hierarchy is too deep.");
  }

  DCOUT("ReconstructPrimRecursively: parent = "
        << std::to_string(parent) << ", current = " << current
        << ", level = " << std::to_string(level));

  if ((current < 0) || (current >= int(_nodes->size()))) {
    PUSH_ERROR("Invalid current node id: " + std::to_string(current) +
               ". Must be in range [0, " + std::to_string(_nodes->size()) + ")");
    return false;
  }

  // TODO: Refactor

  // null : parent node is Property or other Spec type.
  // non-null : parent node is PrimSpec
  PrimSpec *currPrimSpecPtr = nullptr;
  PrimSpec *primspecPtr{nullptr};

  // Assume parent node is already processed.
  bool is_parent_variant = _variantPrims.count(parent);

  if (!ReconstructPrimSpecNode(parent, current, level, is_parent_variant, psmap,
                           layer, primspecPtr)) {
    return false;
  }

  if (primspecPtr) {
    currPrimSpecPtr = primspecPtr;
  }

  {
    const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];
    DCOUT("node.Children.size = " << node.GetChildren().size());
    for (size_t i = 0; i < node.GetChildren().size(); i++) {
      DCOUT("Reconstuct Prim children: " << i << " / "
                                         << node.GetChildren().size());
      if (!ReconstructPrimSpecRecursively(current, int(node.GetChildren()[i]),
                                      currPrimSpecPtr, level + 1, psmap, layer)) {
        return false;
      }
      DCOUT("DONE Reconstuct PrimSpec children: " << i << " / "
                                              << node.GetChildren().size());
    }
  }

  //
  // Reonstruct variant
  //
  DCOUT(fmt::format("---- reconstruct variant ---"));
  DCOUT(fmt::format("parent {}, current {}", parent, current));

  DCOUT(fmt::format("  has variant properties {}, has variant children {}",
    _variantPropChildren.count(current),
    _variantPrimChildren.count(current)));

  if (_variantPropChildren.count(current)) {

    // - parentPrim
    //   - variantPrim(SpecTypeVariant) <- current
    //     - variant property(SpecTypeAttribute)

    //
    // `current` must be VariantPrim and `parentPrim` should exist
    //
    if (!_variantPrims.count(current)) {
      PUSH_ERROR_AND_RETURN("Internal error: variant attribute is not a child of VariantPrim.");
    }

    if (!parentPrimSpec) {
      PUSH_ERROR_AND_RETURN("Internal error: parentPrimSpec should exist.");
    }

    const Prim &variantPrim = _variantPrims.at(current);

    DCOUT("variant prim name: " << variantPrim.element_name());


    // element_name must be variant: "{variant=value}"
    if (!is_variantElementName(variantPrim.element_name())) {
      PUSH_ERROR_AND_RETURN("Corrupted Crate. VariantAttribute is not the child of VariantPrim.");
    }

    std::array<std::string, 2> toks;
    if (!tokenize_variantElement(variantPrim.element_name(), &toks)) {
      PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
    }

    std::string variantSetName = toks[0];
    std::string variantName = toks[1];

    PrimSpec variant;

    for (const auto &item : _variantPropChildren.at(current)) {
      // item should exist in _variantProps.
      if (!_variantProps.count(item)) {
        PUSH_ERROR_AND_RETURN("Internal error: variant Property not found.");
      }
      const std::pair<Path, Property> &pp = _variantProps.at(item);

      std::string prop_name = std::get<0>(pp).prop_part();
      DCOUT(fmt::format("  node_index = {}, prop name {}", item, prop_name));

      variant.props()[prop_name] = std::move(std::get<1>(pp));
    }

    VariantSetSpec &vs = parentPrimSpec->variantSets()[variantSetName];

    if (vs.name.empty()) {
      vs.name = variantSetName;
    }
    vs.variantSet[variantName] = variant;

  }

  if (_variantPrimChildren.count(current)) {

    // - currentPrim <- current
    //   - variant Prim children

    if (!primspecPtr) {
      PUSH_ERROR_AND_RETURN("Internal error: must be Prim.");
    }

    DCOUT(fmt::format("{} has variant PrimSpec ", primspecPtr->name()));


    for (const auto &item : _variantPrimChildren.at(current)) {

      if (!_variantPrimSpecs.count(item)) {
        PUSH_ERROR_AND_RETURN("Internal error: variant Prim children not found.");
      }

      const PrimSpec &vp = _variantPrimSpecs.at(item);

      DCOUT(fmt::format("  idx {}, variantPrim name {}", item, vp.name()));

      // element_name must be variant: "{variant=value}"
      if (!is_variantElementName(vp.name())) {
        PUSH_ERROR_AND_RETURN("Corrupted Crate. Variant Prim has invalid element_name.");
      }

      std::array<std::string, 2> toks;
      if (!tokenize_variantElement(vp.name(), &toks)) {
        PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
      }

      std::string variantSetName = toks[0];
      std::string variantName = toks[1];

      VariantSetSpec &vs = primspecPtr->variantSets()[variantSetName];

      if (vs.name.empty()) {
        vs.name = variantSetName;
      }
      vs.variantSet[variantName].metas() = vp.metas();
      DCOUT("# of primChildren = " << vp.children().size());
      vs.variantSet[variantName].children() = std::move(vp.children());

    }
  }

  DCOUT(fmt::format("-<---"));

  if (parent == 0) {  // root prim
    if (primspecPtr) {
      //TUSDZ_LOG_I("root primspec.add"); 
      std::string name = primspecPtr->name();

      // memopt
      layer->primspecs()[name] = std::move(*primspecPtr);
      //layer->primspecs()[name] = *primspecPtr;
      //TUSDZ_LOG_I("root primspec.add done"); 
    }
  } else {
    if (_variantPrimSpecs.count(parent)) {
      // Add to variantPrim
      DCOUT("parent is variantPrim: " << parent);
      if (!primspecPtr) {
        // FIXME: Validate current should be Prim.
        PUSH_WARN("parent is variantPrim, but current is not Prim.");
      } else {
        DCOUT("Adding prim to child...");
        PrimSpec &vps = _variantPrimSpecs.at(parent);
        vps.children().emplace_back(std::move(*primspecPtr));
      }
    } else if (primspecPtr && parentPrimSpec) {
      // Add to parent prim.
      //TUSDZ_LOG_I("children.add"); 
      //parentPrimSpec->children().emplace_back(std::move(*primspecPtr));
      

      // memopt
      parentPrimSpec->children().resize(parentPrimSpec->children().size() + 1);
      parentPrimSpec->children().back() = std::move(*primspecPtr);
      //TUSDZ_LOG_I("children.add done"); 
    }
  }

  return true;
}

bool USDCReader::Impl::ToLayer(Layer *layer) {

  if (!layer) {
    PUSH_ERROR_AND_RETURN("`layer` argument is nullptr.");
  }

  // format test
  DCOUT(fmt::format("# of Paths = {}", crate_reader->NumPaths()));

  if (crate_reader->NumNodes() == 0) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  // Use references to avoid copying data from crate_reader
  _nodes = &crate_reader->GetNodes();
  _specs = &crate_reader->GetSpecs();
  _fields = &crate_reader->GetFields();
  _fieldset_indices = &crate_reader->GetFieldsetIndices();
  _paths = &crate_reader->GetPaths();
  _elemPaths = &crate_reader->GetElemPaths();
  _live_fieldsets = &crate_reader->GetLiveFieldSets();

  PathIndexToSpecIndexMap
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs->size(); i++) {
      if ((*_specs)[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      if (path_index_to_spec_index_map.count((*_specs)[i].path_index.value) != 0) {
        PUSH_ERROR_AND_RETURN("Multiple PathIndex found in Crate data.");
      }

      DCOUT(fmt::format("path index[{}] -> spec index [{}]",
                        (*_specs)[i].path_index.value, uint32_t(i)));
      path_index_to_spec_index_map[(*_specs)[i].path_index.value] = uint32_t(i);
    }
  }

  layer->primspecs().clear();

  int root_node_id = 0;
  bool ret = ReconstructPrimSpecRecursively(/* no further root for root_node */ -1,
                                        root_node_id, /* root Prim */ nullptr,
                                        /* level */ 0,
                                        path_index_to_spec_index_map, layer);

  if (!ret) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Layer(PrimSpec hierarchy)");
  }

  //stage->compute_absolute_prim_path_and_assign_prim_id();

  return true;
}

bool USDCReader::Impl::ReadUSDC() {
  TINYUSDZ_PROFILE_FUNCTION("usdc-reader");
  if (crate_reader) {
    delete crate_reader;
  }

  // Setup CrateReaderConfig.
  crate::CrateReaderConfig config;

  // Transfer settings
  config.numThreads = _config.numThreads;
  config.use_mmap = _config.use_mmap || _config.mmap_zero_copy;  // Enable mmap for memory optimization

  size_t sz_mb = _config.kMaxAllowedMemoryInMB;
  if (sizeof(size_t) == 4) {
    // 32bit
    // cap to 2GB
    sz_mb = (std::min)(size_t(1024 * 2), sz_mb);

    config.maxMemoryBudget = sz_mb * 1024 * 1024;
  } else {
    config.maxMemoryBudget = _config.kMaxAllowedMemoryInMB * 1024ull * 1024ull;
  }

  crate_reader = new crate::CrateReader(_sr, config);
  
  // Pass progress callback to crate reader if set
  if (_progress_callback) {
    crate_reader->SetProgressCallback(_progress_callback, _progress_userptr);
  }

  _warn.clear();
  _err.clear();

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadBootStrap");
    if (!crate_reader->ReadBootStrap()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadTOC");
    if (!crate_reader->ReadTOC()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  // Read known sections

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadTokens");
    if (!crate_reader->ReadTokens()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  {
    TINYUSDZ_PROFILE_SCOPE("usdc-reader", "ReadStrings");
    if (!crate_reader->ReadStrings()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();
      return false;
    }
  }

  if (!crate_reader->ReadFields()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadFieldSets()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadPaths()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadSpecs()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  // TODO(syoyo): Read unknown sections

  ///
  /// Reconstruct C++ representation of USD scene graph.
  ///
  if (_config.use_lazy_property_construction) {
    DCOUT("Skip BuildLiveFieldSets (lazy property construction enabled)");
  } else {
    DCOUT("BuildLiveFieldSets");
    if (!crate_reader->BuildLiveFieldSets()) {
      _warn = crate_reader->GetWarning();
      _err = crate_reader->GetError();

      return false;
    }
  }

  // Free decompression buffers — no longer needed after all sections are read
  // and fieldsets are materialized (non-lazy) or will be decoded during
  // ReconstructStage (lazy). In lazy mode, decompression still happens during
  // reconstruction, so we only shrink here in non-lazy mode.
  if (!_config.use_lazy_property_construction) {
    crate_reader->ShrinkDecompressionBuffers();
  }

  _warn += crate_reader->GetWarning();
  _err += crate_reader->GetError();

  DCOUT("Read Crate.");

  // Report final progress (100%)
  if (_progress_callback) {
    _progress_callback(1.0f, _progress_userptr);
  }

  return true;
}

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, const USDCReaderConfig &config) {
  impl_ = new USDCReader::Impl(sr, config);
}

USDCReader::~USDCReader() {
  delete impl_;
  impl_ = nullptr;
}

void USDCReader::set_reader_config(const USDCReaderConfig &config) {
  impl_->set_reader_config(config);
}

const USDCReaderConfig USDCReader::get_reader_config() const {
  return impl_->get_reader_config();
}

void USDCReader::SetProgressCallback(ProgressCallback callback, void *userptr) {
  impl_->set_progress_callback(callback, userptr);
}

bool USDCReader::ReconstructStage(Stage *stage) {
  DCOUT("Reconstruct Stage.");
  return impl_->ReconstructStage(stage);
}

bool USDCReader::get_as_layer(Layer *layer) {
  return impl_->ToLayer(layer);
}

std::string USDCReader::GetError() { return impl_->GetError(); }

std::string USDCReader::GetWarning() { return impl_->GetWarning(); }

bool USDCReader::ReadUSDC() { return impl_->ReadUSDC(); }

size_t USDCReader::GetMemoryUsage() const {
  return impl_->GetMemoryUsage();
}

USDCMemoryUsageReport USDCReader::GetMemoryUsageReport() const {
  return impl_->GetMemoryUsageReport();
}

}  // namespace usdc
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDC_READER

namespace tinyusdz {
namespace usdc {

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, const USDCReaderConfig &config) {
  (void)sr;
  (void)config;
}

USDCReader::~USDCReader() {}

void USDCReader::set_reader_config(const USDCReaderConfig &config) {
  (void)config;
}

const USDCReaderConfig USDCReader::get_reader_config() const {
  return USDCReaderConfig();
}

bool USDCReader::ReconstructStage(Stage *stage) {
  (void)stage;
  DCOUT("Reconstruct Stage.");
  return false;
}

bool USDCReader::get_as_layer(Layer *layer) {
  (void)layer;
  return false;
}

std::string USDCReader::GetError() {
  return "USDC reader feature is disabled in this build.\n";
}

std::string USDCReader::GetWarning() { return ""; }

size_t USDCReader::GetMemoryUsage() const { return 0; }

USDCMemoryUsageReport USDCReader::GetMemoryUsageReport() const {
  return USDCMemoryUsageReport();
}

}  // namespace usdc
}  // namespace tinyusdz

#endif  // TINYUSDZ_DISABLE_MODULE_USDC_READER
