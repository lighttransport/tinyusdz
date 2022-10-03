// SPDX-License-Identifier: MIT
// Copyright 2020-Present Syoyo Fujita.
//
// USDC(Crate) reader
//
// TODO:
//
// - [ ] Validate the existence of connection Paths(Connection) and target
// Paths(Relation)
// - [ ] GeomSubset
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "usdc-reader.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_READER)

#include <stack>
#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"
#include "tinyusdz.hh"
#include "value-types.hh"
#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"
#include "crate-pprint.hh"
#include "crate-reader.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "path-util.hh"
#include "pprinter.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tiny-format.hh"
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
#include "common-macros.inc"

namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty)                                      \
  template <>                                                            \
  bool ReconstructPrim<__ty>(const PropertyMap &, const ReferenceList &, \
                             __ty *, std::string *, std::string *)

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
RECONSTRUCT_PRIM_DECL(GeomBasisCurves);
RECONSTRUCT_PRIM_DECL(GeomCamera);
RECONSTRUCT_PRIM_DECL(LuxSphereLight);
RECONSTRUCT_PRIM_DECL(LuxDomeLight);
RECONSTRUCT_PRIM_DECL(LuxDiskLight);
RECONSTRUCT_PRIM_DECL(LuxDistantLight);
RECONSTRUCT_PRIM_DECL(LuxCylinderLight);
RECONSTRUCT_PRIM_DECL(SkelRoot);
RECONSTRUCT_PRIM_DECL(SkelAnimation);
RECONSTRUCT_PRIM_DECL(Skeleton);
RECONSTRUCT_PRIM_DECL(BlendShape);
RECONSTRUCT_PRIM_DECL(Material);
RECONSTRUCT_PRIM_DECL(Shader);

#undef RECONSTRUCT_PRIM_DECL

}  // namespace prim

namespace usdc {

constexpr auto kTag = "[USDC]";

#if 0
struct PrimNode {
  value::Value
      prim;  // Usually stores reconstructed(composed) concrete Prim(e.g. Xform)

  int64_t parent{-1};            // -1 = root node
  std::vector<size_t> children;  // index to USDCReader::Impl::._prim_nodes[]

  //
  //
  //
  std::vector<value::token> primChildren;

#if 0
  //
  // For Variants
  ///
  // SpecType::VariantSet
  std::vector<value::token> variantChildren;

  // TODO: Variants
  std::map<std::string, value::Value> variants;
#else

#endif
};

struct VariantPrimNode {
  int64_t parent{-1};  // -1 = root node

  //
  // For Variants
  ///
  // SpecType::VariantSet
  std::vector<value::token> variantChildren;

  value::Value prim;  // Usually stores reconstructed Prim(e.g. Xform)
};
#endif

class USDCReader::Impl {
 public:
  Impl(StreamReader *sr, const USDCReaderConfig &config) : _sr(sr) {
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

  ~Impl() {
    delete crate_reader;
    crate_reader = nullptr;
  }

  bool ReadUSDC();

  using PathIndexToSpecIndexMap = std::unordered_map<uint32_t, uint32_t>;

  ///
  /// Construct Property(Attribute, Relationship/Connection) from
  /// FieldValuePairs
  ///
  bool ParseProperty(const SpecType specType,
                     const crate::FieldValuePairVector &fvs, Property *prop);

  // For simple, non animatable and non `.connect` types. e.g. "token[]"
  template <typename T>
  bool ReconstructSimpleAttribute(int parent,
                                  const crate::FieldValuePairVector &fvs,
                                  T *attr, bool *custom_out = nullptr,
                                  Variability *variability_out = nullptr);

#if 0
  // For attribute which maybe a value, connection or TimeSamples.
  template <typename T>
  bool ReconstructTypedProperty(int parent,
                                const crate::FieldValuePairVector &fvs,
                                TypedProperty<T> *attr);
#endif

  ///
  /// Parse Prim spec from FieldValuePairs
  ///
  bool ParsePrimSpec(const crate::FieldValuePairVector &fvs,
                       nonstd::optional<std::string> &typeName, /* out */
                       nonstd::optional<Specifier> &specifier,  /* out */
                       std::vector<value::token> &properties,   /* out */
                       PrimMeta &primMeta);                     /* out */

  bool ParseVariantSetFields(
      const crate::FieldValuePairVector &fvs,
      std::vector<value::token> &variantChildren); /* out */

  template <typename T>
  bool ReconstructPrim(const crate::CrateReader::Node &node,
                       const crate::FieldValuePairVector &fvs,
                       const PathIndexToSpecIndexMap &psmap, T *prim);

  ///
  /// Reconstrcut Prim node.
  /// Returns reconstruct Prim to `primOut`
  /// When `current` is 0(StageMeta), `primOut` is not set.
  /// `is_parent_variant` : True when parent path is Variant
  ///
  bool ReconstructPrimNode(int parent, int current, int level, bool is_parent_variant,
                           const PathIndexToSpecIndexMap &psmap, Stage *stage,
                           nonstd::optional<Prim> *primOut);

  ///
  /// Reconstruct Prim from given `typeName` string(e.g. "Xform")
  ///
  nonstd::optional<Prim> ReconstructPrimFromTypeName(
      const std::string &typeName, const std::string &prim_name,
      const crate::CrateReader::Node &node,
      const crate::FieldValuePairVector &fvs,
      const PathIndexToSpecIndexMap &psmap, const PrimMeta &meta);

  bool ReconstructPrimRecursively(int parent_id, int current_id, Prim *rootPrim,
                                  int level,
                                  const PathIndexToSpecIndexMap &psmap,
                                  Stage *stage);

  bool ReconstructPrimTree(Prim *rootPrim, const PathIndexToSpecIndexMap &psmap,
                           Stage *stage);

  bool ReconstructStage(Stage *stage);

  ///
  /// --------------------------------------------------
  ///

  void PushError(const std::string &s) { _err += s; }

  void PushWarn(const std::string &s) { _warn += s; }

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const { return memory_used / (1024 * 1024); }

 private:
  nonstd::expected<APISchemas, std::string> ToAPISchemas(
      const ListOp<value::token> &);

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

  bool ReconstrcutStageMeta(const crate::FieldValuePairVector &fvs,
                            StageMetas *out,
                            std::vector<value::token> *primChildrenOut);

  bool AddVariantChildrenToPrimNode(
      int32_t prim_idx, const std::vector<value::token> &variantChildren) {
    if (prim_idx < 0) {
      return false;
    }

    if (_variantChildren.count(uint32_t(prim_idx))) {
      PUSH_WARN("Multiple Field with VariantSet SpecType detected.");
    }

    _variantChildren[uint32_t(prim_idx)] = variantChildren;

    return true;
  }

  bool AddVariantToPrimNode(int32_t prim_idx, const value::Value &variant);

  crate::CrateReader *crate_reader{nullptr};

  StreamReader *_sr = nullptr;
  std::string _err;
  std::string _warn;

  USDCReaderConfig _config;

  // Tracks the memory used(In advisorily manner since counting memory usage is
  // done by manually, so not all memory consumption could be tracked)
  size_t memory_used{0};  // in bytes.

  nonstd::optional<Path> GetPath(crate::Index index) const {
    if (index.value <= _paths.size()) {
      return _paths[index.value];
    }

    return nonstd::nullopt;
  }

  nonstd::optional<Path> GetElemPath(crate::Index index) const {
    if (index.value <= _elemPaths.size()) {
      return _elemPaths[index.value];
    }

    return nonstd::nullopt;
  }

  // TODO: Do not copy data from crate_reader.
  std::vector<crate::CrateReader::Node> _nodes;
  std::vector<crate::Spec> _specs;
  std::vector<crate::Field> _fields;
  std::vector<crate::Index> _fieldset_indices;
  std::vector<crate::Index> _string_indices;
  std::vector<Path> _paths;
  std::vector<Path> _elemPaths;

  std::map<crate::Index, crate::FieldValuePairVector>
      _live_fieldsets;  // <fieldset index, List of field with unpacked Values>

  // std::vector<PrimNode> _prim_nodes;

  // VariantSet Spec. variantChildren
  std::map<uint32_t, std::vector<value::token>> _variantChildren;

  // For Prim/Props defined as Variant(SpecType::VariantSet)
  // key = path index.
  std::map<int32_t, Prim> _variantPrims;
  std::map<uint32_t, Property> _variantAttributeNodes;

  std::set<int32_t> _variant_prim_table;

  // Check if given node_id is a prim node.
  std::set<int32_t> _prim_table;
};

//
// -- Impl
//

#if 0

bool USDCReader::Impl::ReconstructGeomSubset(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomSubset *geom_subset) {

  DCOUT("Reconstruct GeomSubset");

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

#endif

namespace {}

nonstd::expected<APISchemas, std::string> USDCReader::Impl::ToAPISchemas(
    const ListOp<value::token> &arg) {
  APISchemas schemas;

  auto SchemaHandler =
      [](const value::token &tok) -> nonstd::optional<APISchemas::APIName> {
    if (tok.str() == "MaterialBindingAPI") {
      return APISchemas::APIName::MaterialBindingAPI;
    } else if (tok.str() == "SkelBindingAPI") {
      return APISchemas::APIName::SkelBindingAPI;
    } else if (tok.str() == "Preliminary_AnchoringAPI") {
      return APISchemas::APIName::Preliminary_AnchoringAPI;
    } else if (tok.str() == "Preliminary_PhysicsColliderAPI") {
      return APISchemas::APIName::Preliminary_PhysicsColliderAPI;
    } else if (tok.str() == "Preliminary_PhysicsMaterialAPI") {
      return APISchemas::APIName::Preliminary_PhysicsMaterialAPI;
    } else if (tok.str() == "Preliminary_PhysicsRigidBodyAPI") {
      return APISchemas::APIName::Preliminary_PhysicsRigidBodyAPI;
    } else {
      return nonstd::nullopt;
    }
  };

  if (arg.IsExplicit()) {  // fast path
    for (auto &item : arg.GetExplicitItems()) {
      if (auto pv = SchemaHandler(item)) {
        std::string instanceName = "";  // TODO
        schemas.names.push_back({pv.value(), instanceName});
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

  return std::move(dst);
}

bool USDCReader::Impl::BuildPropertyMap(const std::vector<size_t> &pathIndices,
                                        const PathIndexToSpecIndexMap &psmap,
                                        prim::PropertyMap *props) {
  for (size_t i = 0; i < pathIndices.size(); i++) {
    int child_index = int(pathIndices[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    if (!psmap.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index = psmap.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

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

    DCOUT("Path prim part: " << path.value().GetPrimPart()
                             << ", prop part: " << path.value().GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const crate::FieldValuePairVector &child_fvs =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.value().GetPropPart();

      Property prop;
      if (!ParseProperty(spec.spec_type, child_fvs, &prop)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            fmt::format(
                "Failed to construct Property `{}` from FieldValuePairVector.",
                prop_name));
      }

      props->emplace(prop_name, prop);
      DCOUT("Add property : " << prop_name);
    }
  }

  return true;
}

// TODO: Use template and move code to `value-types.hh`
// TODO: Preserve type for Role type(currently, "color3f" is converted to
// "float3" type)
static bool UpcastType(const std::string &reqType, value::Value &inout) {
  // `reqType` may be Role type. Get underlying type
  uint32_t tyid;
  if (auto pv = value::TryGetUnderlyingTypeId(reqType)) {
    tyid = pv.value();
  } else {
    // Invalid reqType.
    return false;
  }

  if (tyid == value::TYPE_ID_FLOAT) {
    float dst;
    if (auto pv = inout.get_value<value::half>()) {
      dst = half_to_float(pv.value());
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_FLOAT2) {
    value::float2 dst;
    if (auto pv = inout.get_value<value::half2>()) {
      value::half2 v = pv.value();
      dst[0] = half_to_float(v[0]);
      dst[1] = half_to_float(v[1]);
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_FLOAT3) {
    value::float3 dst;
    if (auto pv = inout.get_value<value::half3>()) {
      value::half3 v = pv.value();
      dst[0] = half_to_float(v[0]);
      dst[1] = half_to_float(v[1]);
      dst[2] = half_to_float(v[2]);
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_FLOAT4) {
    value::float4 dst;
    if (auto pv = inout.get_value<value::half4>()) {
      value::half4 v = pv.value();
      dst[0] = half_to_float(v[0]);
      dst[1] = half_to_float(v[1]);
      dst[2] = half_to_float(v[2]);
      dst[3] = half_to_float(v[3]);
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_DOUBLE) {
    double dst;
    if (auto pv = inout.get_value<value::half>()) {
      dst = double(half_to_float(pv.value()));
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_DOUBLE2) {
    value::double2 dst;
    if (auto pv = inout.get_value<value::half2>()) {
      value::half2 v = pv.value();
      dst[0] = double(half_to_float(v[0]));
      dst[1] = double(half_to_float(v[1]));
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_DOUBLE3) {
    value::double3 dst;
    if (auto pv = inout.get_value<value::half3>()) {
      value::half3 v = pv.value();
      dst[0] = double(half_to_float(v[0]));
      dst[1] = double(half_to_float(v[1]));
      dst[2] = double(half_to_float(v[2]));
      inout = dst;
      return true;
    }
  } else if (tyid == value::TYPE_ID_DOUBLE4) {
    value::double4 dst;
    if (auto pv = inout.get_value<value::half4>()) {
      value::half4 v = pv.value();
      dst[0] = double(half_to_float(v[0]));
      dst[1] = double(half_to_float(v[1]));
      dst[2] = double(half_to_float(v[2]));
      dst[3] = double(half_to_float(v[3]));
      inout = dst;
      return true;
    }
  }

  return false;
}

/// Attrib/Property fieldSet example
///
///   specTyppe = SpecTypeConnection
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
                                     const crate::FieldValuePairVector &fvs,
                                     Property *prop) {
  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  bool custom{false};
  nonstd::optional<value::token> typeName;
  nonstd::optional<Interpolation> interpolation;
  nonstd::optional<int> elementSize;
  nonstd::optional<bool> hidden;
  nonstd::optional<CustomDataType> customData;
  nonstd::optional<StringData> comment;
  Property::Type propType{Property::Type::EmptyAttrib};
  PrimAttrib attr;

  bool is_scalar{false};

  value::Value scalar;
  Relation rel;

  // for consistency check
  bool hasConnectionChildren{false};
  bool hasConnectionPaths{false};
  bool hasTargetChildren{false};
  bool hasTargetPaths{false};

  // TODO: Rel, TimeSamples, Connection

  DCOUT("== List of Fields");
  for (auto &fv : fvs) {
    DCOUT(" fv name " << fv.first << "(type = " << fv.second.type_name()
                      << ")");

    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        custom = pv.value();
        DCOUT("  custom = " << pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "variability") {
      if (auto pv = fv.second.get_value<Variability>()) {
        attr.variability = pv.value();
        DCOUT("  variability = " << to_string(attr.variability));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variability` field is not `varibility` type.");
      }
    } else if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("  typeName = " << pv.value().str());
        typeName = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    } else if (fv.first == "default") {
      propType = Property::Type::Attrib;

      // Set scalar
      // TODO: Easier CrateValue to PrimAttrib.var conversion
      scalar = fv.second.get_raw();
      is_scalar = true;

    } else if (fv.first == "timeSamples") {
      propType = Property::Type::Attrib;

      if (auto pv = fv.second.get_value<value::TimeSamples>()) {
        primvar::PrimVar var;
        var.set_timesamples(pv.value());
        attr.set_var(std::move(var));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`timeSamples` is not TimeSamples data.");
      }
    } else if (fv.first == "interpolation") {
      propType = Property::Type::Attrib;

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
      // .connect
      propType = Property::Type::Connection;
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

        if (items.size() == 1) {
          // Single
          const Path path = items[0];

          rel.Set(path);

        } else {
          rel.Set(items);  // [Path]
        }

      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`connectionPaths` field is not `ListOp[Path]` type.");
      }
    } else if (fv.first == "targetPaths") {
      // `rel`
      propType = Property::Type::Relation;
      hasTargetPaths = true;

      if (auto pv = fv.second.get_value<ListOp<Path>>()) {
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

          rel.Set(path);

        } else {
          rel.Set(items);  // [Path]
        }

        rel.SetListEditQualifier(qual);

      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`targetPaths` field is not `ListOp[Path]` type.");
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
          PUSH_ERROR_AND_RETURN_TAG(
              kTag,
              fmt::format("`elementSize` must be within [{}, {}), but got {}",
                          1, _config.kMaxElementSize, p));
        }

        elementSize = p;

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`elementSize` field is not `int` type.");
      }
    } else if (fv.first == "targetChildren") {
      // `targetChildren` seems optionally exist to validate the existence of
      // target Paths when `targetPaths` field exists.
      // TODO: validate path of `targetChildren`
      hasTargetChildren = true;

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
      hasConnectionChildren = true;

      // Path vector
      if (auto pv = fv.second.get_value<std::vector<Path>>()) {
        DCOUT("connectionChildren = " << pv.value());
        // PUSH_WARN("TODO: connectionChildren");
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`connectionChildren` field is not `PathVector` type.");
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
        StringData s;
        s.value = pv.value();
        s.is_triple_quoted = hasNewline(s.value);
        comment = s;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`comment` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else {
      PUSH_WARN("TODO: " << fv.first);
      DCOUT("TODO: " << fv.first);
    }
  }
  DCOUT("== End List of Fields");

  // Post check
#if 0
  if (hasConnectionChildren) {
    // Validate existence of Path..
  }

  if (hasTargetChildren) {
    // Validate existence of Path..
  }
#else
  (void)hasTargetPaths;
  (void)hasTargetChildren;
  (void)hasConnectionChildren;
  (void)hasConnectionPaths;
#endif

  if (is_scalar) {
    if (typeName) {
      // Some inlined? value uses less accuracy type(e.g. `half3`) than
      // typeName(e.g. `float3`) Use type specified in `typeName` as much as
      // possible.
      std::string reqTy = typeName.value().str();
      std::string scalarTy = scalar.type_name();

      if (reqTy.compare(scalarTy) != 0) {
        bool ret = UpcastType(reqTy, scalar);
        if (ret) {
          DCOUT(fmt::format("Upcast type from {} to {}.", scalarTy, reqTy));
        }
      }
    }
    primvar::PrimVar var;
    var.set_scalar(scalar);
    attr.set_var(std::move(var));
  }

  // metas
  if (interpolation) {
    attr.meta.interpolation = interpolation.value();
  }
  if (elementSize) {
    attr.meta.elementSize = elementSize.value();
  }
  if (hidden) {
    attr.meta.hidden = hidden.value();
  }
  if (customData) {
    attr.meta.customData = customData.value();
  }
  if (comment) {
    attr.meta.comment = comment.value();
  }

  // FIXME: SpecType supercedes propType.
  if (propType == Property::Type::EmptyAttrib) {
    if (typeName) {
      (*prop) = Property(typeName.value().str(), custom);
    } else {
      DCOUT("spec_type = " << to_string(spec_type));
      if (spec_type == SpecType::Relationship) {
        // `rel` with no target. e.g. `rel target`
        rel = Relation();
        rel.SetEmpty();
        (*prop) = Property(rel, custom);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`typeName` field is missing.");
      }
    }
  } else if (propType == Property::Type::Attrib) {
    (*prop) = Property(attr, custom);
  } else if (propType == Property::Type::Connection) {
    if (!typeName) {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "`typeName` field is missing for Attribute Connection.");
    }
    (*prop) = Property(rel, typeName.value().str(), custom);
  } else if (propType == Property::Type::Relation) {
    (*prop) = Property(rel, custom);
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "TODO:");
  }

  return true;
}

template <typename T>
bool USDCReader::Impl::ReconstructSimpleAttribute(
    int parent, const crate::FieldValuePairVector &fvs, T *attr,
    bool *custom_out, Variability *variability_out) {
  (void)attr;

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  bool valid{false};

  for (auto &fv : fvs) {
    // Some predefined fields
    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        if (custom_out) {
          (*custom_out) = pv.value();
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "variability") {
      if (auto pv = fv.second.get_value<Variability>()) {
        if (variability_out) {
          (*variability_out) = pv.value();
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variability` field is not `varibility` type.");
      }
    } else if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("typeName = " << pv.value().str());
        if (value::TypeTrait<T>::type_name() != pv.value().str()) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Property type mismatch. `"
                        << value::TypeTrait<T>::type_name()
                        << "` expected but got `" << pv.value().str() << "`.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    } else if (fv.first == "default") {
      if (fv.second.type_id() != value::TypeTrait<T>::type_id) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Property type mismatch. `"
                                            << value::TypeTrait<T>::type_name()
                                            << "` expected but got `"
                                            << fv.second.type_name() << "`.");
      }

      if (auto pv = fv.second.get_value<T>()) {
        (*attr) = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Type mismatch. Internal error.");
      }
      valid = true;
    }

    DCOUT("parent[" << parent << "] fv name " << fv.first
                    << "(type = " << fv.second.type_name() << ")");
  }

  if (!valid) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`default` field not found.");
  }

  return true;
}

#if 0
template <typename T>
bool USDCReader::Impl::ReconstructTypedProperty(
    int parent, const crate::FieldValuePairVector &fvs,
    TypedProperty<T> *attr) {
  (void)attr;

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  for (auto &fv : fvs) {
    // Some predefined fields
    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        attr->custom = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("typeName = " << pv.value().str());
        if (value::TypeTrait<T>::type_name() != pv.value().str()) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Property type mismatch. `"
                        << value::TypeTrait<T>::type_name()
                        << "` expected but got `" << pv.value().str() << "`.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    } else if (fv.first == "default") {
      if (fv.second.type_id() != value::TypeTrait<T>::type_id) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Property type mismatch. `"
                                            << value::TypeTrait<T>::type_name()
                                            << "` expected but got `"
                                            << fv.second.type_name() << "`.");
      }

      if (auto pv = fv.second.get_value<T>()) {
        Animatable<T> anim;
        anim.value = pv.value();
        attr->value = anim;
      }
    }

    DCOUT("parent[" << parent << "] fv name " << fv.first
                    << "(type = " << fv.second.type_name() << ")");
  }

  return true;
}
#endif

template <typename T>
bool USDCReader::Impl::ReconstructPrim(const crate::CrateReader::Node &node,
                                       const crate::FieldValuePairVector &fvs,
                                       const PathIndexToSpecIndexMap &psmap,
                                       T *prim) {
  (void)fvs;

  // Prim's properties are stored in its children nodes.
  prim::PropertyMap properties;
  if (!BuildPropertyMap(node.GetChildren(), psmap, &properties)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
  }

  prim::ReferenceList refs;  // TODO:
  std::string err;

  if (!prim::ReconstructPrim<T>(properties, refs, prim, &_warn, &err)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, err);
  }

  return true;
}

bool USDCReader::Impl::ReconstrcutStageMeta(
    const crate::FieldValuePairVector &fvs, StageMetas *metas,
    std::vector<value::token> *primChildren) {
  /// Stage(toplevel layer) Meta fieldSet example.
  ///
  ///   specTy = SpecTypePseudoRoot
  ///
  ///     - customLayerData(dict)
  ///     - defaultPrim(token)
  ///     - metersPerUnit(double)
  ///     - timeCodesPerSecond(double)
  ///     - upAxis(token)
  ///     - primChildren(token[]) : Crate only. List of root prims
  ///     - documentation(string) : `doc`
  ///     - comment(string) : comment

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
      DCOUT("upAxis = " << to_string(metas->upAxis.GetValue()));

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
      DCOUT("metersPerUnit = " << metas->metersPerUnit.GetValue());
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
      DCOUT("timeCodesPerSecond = " << metas->timeCodesPerSecond.GetValue());
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
      DCOUT("startimeCode = " << metas->startTimeCode.GetValue());
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
      DCOUT("endTimeCode = " << metas->endTimeCode.GetValue());
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
    } else if (fv.first == "primChildren") {  // it looks only appears in USDC.
      auto v = fv.second.get_value<std::vector<value::token>>();
      if (!v) {
        PUSH_ERROR("Type must be `token[]` for `primChildren`, but got " +
                   fv.second.type_name() + "\n");
        return false;
      }

      if (primChildren) {
        (*primChildren) = v.value();
        DCOUT("primChildren = " << (*primChildren));
      }
    } else if (fv.first == "documentation") {  // 'doc'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR("Type must be `string` for `documentation`, but got " +
                   fv.second.type_name() + "\n");
        return false;
      }
      StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->doc = sdata;
      DCOUT("doc = " << metas->doc.value);
    } else if (fv.first == "comment") {  // 'comment'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR("Type must be `string` for `comment`, but got " +
                   fv.second.type_name() + "\n");
        return false;
      }
      StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->comment = sdata;
      DCOUT("comment = " << metas->comment.value);
    } else {
      PUSH_WARN("[StageMeta] TODO: " + fv.first + "\n");
    }
  }

  return true;
}

nonstd::optional<Prim> USDCReader::Impl::ReconstructPrimFromTypeName(
    const std::string &typeName, const std::string &prim_name,
    const crate::CrateReader::Node &node,
    const crate::FieldValuePairVector &fvs,
    const PathIndexToSpecIndexMap &psmap, const PrimMeta &meta) {
#define RECONSTRUCT_PRIM(__primty, __node_ty, __prim_name)    \
  if (__node_ty == value::TypeTrait<__primty>::type_name()) { \
    __primty typed_prim;                                      \
    if (!ReconstructPrim(node, fvs, psmap, &typed_prim)) {    \
      PUSH_ERROR("Failed to reconstruct Prim " << __node_ty); \
      return nonstd::nullopt;                                 \
    }                                                         \
    typed_prim.meta = meta;                                   \
    typed_prim.name = __prim_name;                            \
    value::Value primdata = typed_prim;                       \
    return Prim(primdata);                                    \
  } else

  RECONSTRUCT_PRIM(Xform, typeName, prim_name)
  RECONSTRUCT_PRIM(Model, typeName, prim_name)
  RECONSTRUCT_PRIM(Scope, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomMesh, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomPoints, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomCylinder, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomCube, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomCone, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomSphere, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomCapsule, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomBasisCurves, typeName, prim_name)
  RECONSTRUCT_PRIM(GeomCamera, typeName, prim_name)
  // RECONSTRUCT_PRIM(GeomSubset, typeName, prim_name)
  RECONSTRUCT_PRIM(LuxSphereLight, typeName, prim_name)
  RECONSTRUCT_PRIM(LuxDomeLight, typeName, prim_name)
  RECONSTRUCT_PRIM(LuxCylinderLight, typeName, prim_name)
  RECONSTRUCT_PRIM(LuxDiskLight, typeName, prim_name)
  RECONSTRUCT_PRIM(LuxDistantLight, typeName, prim_name)
  RECONSTRUCT_PRIM(SkelRoot, typeName, prim_name)
  RECONSTRUCT_PRIM(Skeleton, typeName, prim_name)
  RECONSTRUCT_PRIM(SkelAnimation, typeName, prim_name)
  RECONSTRUCT_PRIM(BlendShape, typeName, prim_name)
  RECONSTRUCT_PRIM(Shader, typeName, prim_name)
  RECONSTRUCT_PRIM(Material, typeName, prim_name) {
    PUSH_WARN("TODO or unsupported prim type: " << typeName);
    return nonstd::nullopt;
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
///     - properties(token[]) : List of name of Prim properties(attributes)
///     - optional: primChildren(token[]): List of child prims.
///
///
bool USDCReader::Impl::ParsePrimSpec(const crate::FieldValuePairVector &fvs,
                                       nonstd::optional<std::string> &typeName,
                                       nonstd::optional<Specifier> &specifier,
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
        primMeta.primChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`primChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "active") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.active = (*pv);
        DCOUT("active = " << to_string(primMeta.active.value()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`active` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "hidden") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.hidden = (*pv);
        DCOUT("hidden = " << to_string(primMeta.hidden.value()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`hidden` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "assetInfo") {
      // CustomData(dict)
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.assetInfo = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`assetInfo` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "kind") {
      if (auto pv = fv.second.as<value::token>()) {
        if (auto kv = KindFromString(pv->str())) {
          primMeta.kind = kv.value();
        } else {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid token for `kind` Prim metadata: `{}`",
                                pv->str()));
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`kind` must be type `token`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "apiSchemas") {
      if (auto pv = fv.second.as<ListOp<value::token>>()) {
        auto listop = (*pv);

        auto ret = ToAPISchemas(listop);
        if (!ret) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Failed to validate `apiSchemas`: " + ret.error());
        } else {
          primMeta.apiSchemas = (*ret);
        }
        // DCOUT("apiSchemas = " << to_string(listop));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`apiSchemas` must be type `ListOp[Token]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "documentation") {
      if (auto pv = fv.second.as<std::string>()) {
        StringData s;
        s.value = (*pv);
        s.is_triple_quoted = hasNewline(s.value);
        primMeta.doc = s;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`documentation` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "comment") {
      if (auto pv = fv.second.as<std::string>()) {
        StringData s;
        s.value = (*pv);
        s.is_triple_quoted = hasNewline(s.value);
        primMeta.comment = s;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`comment` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "customData") {
      // CustomData(dict)
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.customData = (*pv);
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

        if (ps.size() > 1) {
          // This should not happen though.
          PUSH_WARN(
              "ListOp with multiple ListOpType is not supported for now. Use "
              "the first one: " +
              to_string(std::get<0>(ps[0])));
        }

        auto qual = std::get<0>(ps[0]);
        auto items = std::get<1>(ps[0]);
        auto listop = (*pv);
        primMeta.variantSets = std::make_pair(qual, items);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`variantSetNames` must be type `ListOp[String]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "sceneName") {  // USDZ extension
      // CustomData(dict)
      if (auto pv = fv.second.as<std::string>()) {
        primMeta.sceneName = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`sceneName` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "inherits") {  // `inherits` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        // make empty array
        primMeta.inherits =
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Path>());
      } else if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("inherits = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        if (ps.size() > 1) {
          // This should not happen though.
          PUSH_WARN(
              "ListOp with multiple ListOpType is not supported for now. Use "
              "the first one: " +
              to_string(std::get<0>(ps[0])));
        }

        auto qual = std::get<0>(ps[0]);
        auto items = std::get<1>(ps[0]);
        primMeta.inherits = std::make_pair(qual, items);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`inherits` must be type `path` o `path[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "specializes") {  // `specializes` composition
      if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("specializes = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        if (ps.size() > 1) {
          // This should not happen though.
          PUSH_WARN(
              "ListOp with multiple ListOpType is not supported for now. Use "
              "the first one: " +
              to_string(std::get<0>(ps[0])));
        }

        auto qual = std::get<0>(ps[0]);
        auto items = std::get<1>(ps[0]);
        auto listop = (*pv);
        primMeta.specializes = std::make_pair(qual, items);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`specializes` must be type `ListOp[Path]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "inheritPaths") {  // `specializes` composition
      if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("inheritPaths = " << to_string(p));

        auto ps = DecodeListOp<Path>(p);

        if (ps.size() > 1) {
          // This should not happen though.
          PUSH_WARN(
              "ListOp with multiple ListOpType is not supported for now. Use "
              "the first one: " +
              to_string(std::get<0>(ps[0])));
        }

        auto qual = std::get<0>(ps[0]);
        auto items = std::get<1>(ps[0]);
        auto listop = (*pv);
        primMeta.inheritPaths = std::make_pair(qual, items);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`inheritPaths` must be type `ListOp[Path]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else {
      DCOUT("PrimProp TODO: " << fv.first);
      PUSH_WARN("PrimProp TODO: " << fv.first);
    }
  }

  return true;
}

///
///
/// VariantSet fieldSet example.
///
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
      DCOUT("VariantSet field TODO: " << fv.first);
      PUSH_WARN("VariantSet field TODO: " << fv.first);
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructPrimNode(int parent, int current, int level,
                                           bool is_parent_variant,
                                           const PathIndexToSpecIndexMap &psmap,
                                           Stage *stage,
                                           nonstd::optional<Prim> *primOut) {
  const crate::CrateReader::Node &node = _nodes[size_t(current)];

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << pprint::Indent(uint32_t(level)) << "lv[" << level
            << "] node_index[" << current << "] " << node.GetLocalPath()
            << " ==\n";
  std::cout << pprint::Indent(uint32_t(level)) << " childs = [";
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    std::cout << node.GetChildren()[i];
    if (i != (node.GetChildren().size() - 1)) {
      std::cout << ", ";
    }
  }
  std::cout << "] (is_parent_variant = " << is_parent_variant << ")\n";
#endif

  if (!psmap.count(uint32_t(current))) {
    // No specifier assigned to this node.
    DCOUT("No specifier assigned to this node: " << current);
    return true;  // would be OK.
  }

  uint32_t spec_index = psmap.at(uint32_t(current));
  if (spec_index >= _specs.size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
    return false;
  }

  const crate::Spec &spec = _specs[spec_index];

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

  if (!_live_fieldsets.count(spec.fieldset_index)) {
    PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
               " must exist in live fieldsets.");
    return false;
  }

  const crate::FieldValuePairVector &fvs =
      _live_fieldsets.at(spec.fieldset_index);

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  // DBG
  for (auto &fv : fvs) {
    DCOUT("parent[" << current << "] level [" << level << "] fv name "
                    << fv.first << "(type = " << fv.second.type_name() << ")");
  }

  Path elemPath;

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

    std::vector<value::token> primChildren;
    if (!ReconstrcutStageMeta(fvs, &stage->GetMetas(), &primChildren)) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct StageMeta.");
    }

    // TODO: Validate scene using `primChildren`.

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
      std::vector<value::token> properties;

      PrimMeta primMeta;

      DCOUT("== PrimFields begin ==> ");

      if (!ParsePrimSpec(fvs, typeName, specifier, properties, primMeta)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse Prim fields.");
        return false;
      }

      DCOUT("<== PrimFields end ===");

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
          PUSH_WARN("TODO: `class` specifier. skipping this model...");
          return true;
        } else if (specifier.value() == Specifier::Over) {
          PUSH_WARN("TODO: `over` specifier. skipping this model...");
          return true;
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`specifier` field is missing for FieldSets "
                                  "with SpecType::Prim.");
      }

      if (!typeName) {
        PUSH_WARN("Treat this node as Model(where `typeName` is missing.");
        typeName = "Model";
      }

      if (typeName) {
        std::string prim_name = elemPath.GetPrimPart();

        // Validation check should be already done in crate-reader, so no
        // further validation required.
        if (!ValidatePrimName(prim_name)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Prim name.");
        }

        auto prim = ReconstructPrimFromTypeName(typeName.value(), prim_name,
                                                node, fvs, psmap, primMeta);

        if (prim) {
          // Prim name
          prim.value().elementPath = elemPath;
        }

        if (primOut) {
          (*primOut) = prim;
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
      // Assume parent(Prim) already exists(parsed)
      // TODO: Confirm Crate format allow defining Prim after VariantSet
      // serialization.
      if (!_prim_table.count(parent)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Parent Prim for this VariantSet not found.");
      }

      DCOUT(
          fmt::format("[{}] is a Variantset node(parent = {}). prim_idx? = {}",
                      current, parent, _prim_table.count(current)));

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();

        DCOUT(fmt::format("Element path: {}", elemPath.full_path_name()));

        // Ensure ElementPath is quoted with '{' and '}'
        if (startsWith(elemPath.full_path_name(), "{") && endsWith(elemPath.full_path_name(), "}")) {
          // ok
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));

        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      std::vector<value::token> variantChildren;

      // Only contains `variantChildren` field with type `token[]`

      DCOUT("== VariantSetFields begin ==> ");

      if (!ParseVariantSetFields(fvs, variantChildren)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse VariantSet fields.");
        return false;
      }

      DCOUT("<== VariantSetFields endn === ");

      // Add variantChildren to prim node.
      if (!AddVariantChildrenToPrimNode(parent, variantChildren)) {
        return false;
      }

      break;
    }
    case SpecType::Variant: {
      // Since the Prim this Variant node belongs to is not yet reconstructed
      // during the Prim tree traversal, We manage variant node separately

      DCOUT(fmt::format("[{}] is a Variant node(parent = {}). prim_idx? = {}",
                        current, parent, _prim_table.count(current)));

      nonstd::optional<std::string> typeName;
      nonstd::optional<Specifier> specifier;
      std::vector<value::token> properties;

      PrimMeta primMeta;

      DCOUT("== VariantFields begin ==> ");

      if (!ParsePrimSpec(fvs, typeName, specifier, properties, primMeta)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Failed to parse Prim fields under Variant.");
        return false;
      }

      DCOUT("<== VariantFields end === ");

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
          PUSH_WARN("TODO: `class` specifier. skipping this model...");
          return true;
        } else if (specifier.value() == Specifier::Over) {
          PUSH_WARN("TODO: `over` specifier. skipping this model...");
          return true;
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
        }
      } else {
        // Seems Variant is only composed of Properties.
        // Create pseudo `def` Prim
        specifier = Specifier::Def;
      }

      if (!typeName) {
        PUSH_WARN("Treat this node as Model(where `typeName` is missing.");
        typeName = "Model";
      }

      nonstd::optional<Prim> variantPrim;
      if (typeName) {
        std::string prim_name = elemPath.GetPrimPart();
        DCOUT("elemPath.primPart = " << prim_name);

        // Something like '{shapeVariant=Capsule}'

        // Ensure prim_name is quoted with '{' and '}'
        if (startsWith(prim_name, "{") && endsWith(prim_name, "}")) {
          // ok
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        // Remove variant quotation
        prim_name = unwrap(prim_name, "{", "}");

        // Decompose to variantName, '=', variantValue
        std::vector<std::string> toks = split(prim_name, "=");
        if (toks.size() != 2) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid Variant ElementPath name: `{}`", elemPath));
        }

        std::string variantSetName = toks[0];
        std::string variantPrimName = toks[1];

        if (!ValidatePrimName(variantPrimName)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Invalid Prim name in Variant: `{}`", variantPrimName));
        }

        variantPrim = ReconstructPrimFromTypeName(typeName.value(), variantPrimName,
                                                node, fvs, psmap, primMeta);

        if (variantPrim) {
          // Prim name
          variantPrim.value().elementPath = elemPath; // FIXME: Use variantPrimName?

          // Prim Specifier
          variantPrim.value().specifier = specifier.value();

          // Store variantPrim to temporary buffer.
          DCOUT("add prim idx as variant" << current);
          if (_variantPrims.count(current)) {
            DCOUT("??? prim idx already set " << current);
          } else {
            _variantPrims.emplace(current, variantPrim.value());
          }
        }
      }

      break;
    }
    case SpecType::Attribute: {
      if (is_parent_variant) {

        nonstd::optional<Path> path = GetPath(spec.path_index);

        if (!path) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
        }

        Property prop;
        if (!ParseProperty(spec.spec_type, fvs, &prop)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Failed to parse Attribut: {}.", path.value().GetPropPart()));

        }

        // Parent Prim is not yet reconstructed, so store info to temporary buffer _variantAttributeNodes.
        _variantAttributeNodes.emplace(current, prop);

        DCOUT(fmt::format("[{}] Parsed Attribute {} under Variant. PathIndex {}", current, path.value().GetPropPart(), spec.path_index));

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

bool USDCReader::Impl::ReconstructPrimRecursively(
    int parent, int current, Prim *rootPrim, int level,
    const PathIndexToSpecIndexMap &psmap, Stage *stage) {
  if (level > int32_t(_config.kMaxPrimNestLevel)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Prim hierarchy is too deep.");
  }

  DCOUT("ReconstructPrimRecursively: parent = "
        << std::to_string(current) << ", level = " << std::to_string(level));

  if ((current < 0) || (current >= int(_nodes.size()))) {
    PUSH_ERROR("Invalid current node id: " + std::to_string(current) +
               ". Must be in range [0, " + std::to_string(_nodes.size()) + ")");
    return false;
  }

  crate::Spec spec;
  {
    uint32_t spec_index = psmap.at(uint32_t(current));

    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
      return false;
    }

    spec = _specs[spec_index];
  }

  // TODO: Refactor

  // null : parent node is Property or other Spec type.
  // non-null : parent node is Prim
  Prim *currPrimPtr = nullptr;
  nonstd::optional<Prim> prim;

  bool is_parent_variant = _variant_prim_table.count(parent);

  if (!ReconstructPrimNode(parent, current, level, is_parent_variant, psmap, stage, &prim)) {
    return false;
  }

  if (prim) {
    currPrimPtr = &(prim.value());
  }

  {
    const crate::CrateReader::Node &node = _nodes[size_t(current)];
    DCOUT("node.Children.size = " << node.GetChildren().size());
    for (size_t i = 0; i < node.GetChildren().size(); i++) {
      DCOUT("Reconstuct Prim children: " << i << " / "
                                         << node.GetChildren().size());
      if (!ReconstructPrimRecursively(current, int(node.GetChildren()[i]),
                                      currPrimPtr, level + 1, psmap, stage)) {
        return false;
      }
      DCOUT("DONE Reconstuct Prim children: " << i << " / "
                                              << node.GetChildren().size());
    }
  }

  if (parent == 0) {  // root prim
    if (prim) {
      stage->GetRootPrims().emplace_back(std::move(prim.value()));
    }
  } else {
    // Add to root prim.
    if (prim && rootPrim) {
      rootPrim->children.emplace_back(std::move(prim.value()));
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructStage(Stage *stage) {
  (void)stage;

  // format test
  DCOUT(fmt::format("# of Paths = {}", crate_reader->NumPaths()));

  if (crate_reader->NumNodes() == 0) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  // TODO: Directly access data in crate_reader.
  _nodes = crate_reader->GetNodes();
  _specs = crate_reader->GetSpecs();
  _fields = crate_reader->GetFields();
  _fieldset_indices = crate_reader->GetFieldsetIndices();
  _paths = crate_reader->GetPaths();
  _elemPaths = crate_reader->GetElemPaths();
  _live_fieldsets = crate_reader->GetLiveFieldSets();

  PathIndexToSpecIndexMap
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs.size(); i++) {
      if (_specs[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      if (path_index_to_spec_index_map.count(_specs[i].path_index.value) != 0) {
        PUSH_ERROR_AND_RETURN("Multiple PathIndex found in Crate data.");
      }

      path_index_to_spec_index_map[_specs[i].path_index.value] = uint32_t(i);
    }
  }

  stage->GetRootPrims().clear();

  int root_node_id = 0;
  bool ret = ReconstructPrimRecursively(/* no further root for root_node */ -1,
                                        root_node_id, /* root Prim */ nullptr,
                                        /* level */ 0,
                                        path_index_to_spec_index_map, stage);

  if (!ret) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Stage(Prim hierarchy)");
  }

  return true;
}

bool USDCReader::Impl::ReadUSDC() {
  if (crate_reader) {
    delete crate_reader;
  }

  // TODO: Setup CrateReaderConfig.
  crate::CrateReaderConfig config;

  // Transfer settings
  config.numThreads = _config.numThreads;

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

  _warn.clear();
  _err.clear();

  if (!crate_reader->ReadBootStrap()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadTOC()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  // Read known sections

  if (!crate_reader->ReadTokens()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadStrings()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
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
  DCOUT("BuildLiveFieldSets\n");
  if (!crate_reader->BuildLiveFieldSets()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();

    return false;
  }

  _warn += crate_reader->GetWarning();
  _err += crate_reader->GetError();

  DCOUT("Read Crate.\n");

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

bool USDCReader::ReconstructStage(Stage *stage) {
  DCOUT("Reconstruct Stage.");
  return impl_->ReconstructStage(stage);
}

std::string USDCReader::GetError() { return impl_->GetError(); }

std::string USDCReader::GetWarning() { return impl_->GetWarning(); }

bool USDCReader::ReadUSDC() { return impl_->ReadUSDC(); }

}  // namespace usdc
}  // namespace tinyusdz

#else

namespace tinyusdz {
namespace usdc {

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, USDCReaderConfig &config) {
  (void)sr;
  (void)config;
}

USDCReader::~USDCReader() {}

bool USDCReader::ReconstructStage(Stage *stage) {
  (void)scene;
  DCOUT("Reconstruct Stage.");
  return false;
}

std::string USDCReader::GetError() {
  return "USDC reader feature is disabled in this build.\n";
}

std::string USDCReader::GetWarning() { return ""; }

}  // namespace usdc
}  // namespace tinyusdz

#endif
