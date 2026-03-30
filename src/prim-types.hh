// SPDX-License-Identifier: Apache 2.0

///
/// @file prim-types.hh
/// @brief Core USD primitive type definitions and data structures
///
/// Contains fundamental USD concepts including Prim (primitive), Layer,
/// Properties, Attributes, Relationships, and supporting data structures.
/// These form the building blocks of USD scene graphs.
///
/// This file has been refactored into modular headers in src/prim-types/.
/// For backward compatibility, it includes all modular headers and contains
/// types with complex dependencies (Prim, PrimSpec, PrimNode).
///
#pragma once

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#if defined(TINYUSDZ_ENABLE_THREAD)
#include <mutex>
#include <thread>
#endif

//
#include "value-types.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "handle-allocator.hh"
#include "primvar.hh"
//
#include "value-eval-util.hh"
#include "math-util.inc"

// Include modular headers in dependency order
#include "core/prim-enums.hh"
#include "core/ordered-dict.hh"
#include "core/list-op.hh"
#include "core/path.hh"
#include "core/extent.hh"
#include "core/meta-variable.hh"
#include "core/metadata-base.hh"
#include "core/composition-types.hh"
#include "core/prim-metas.hh"
#include "core/attr-metas.hh"
#include "core/animatable.hh"
#include "core/typed-attribute.hh"
#include "core/relationship.hh"
#include "core/attribute.hh"
#include "core/property.hh"
#include "core/xform-op.hh"
#include "core/collection-api.hh"
#include "core/material-binding.hh"
#include "core/layer-types.hh"

namespace tinyusdz {

//
// Colum-major order(e.g. employed in OpenGL).
// For example, 12th([3][0]), 13th([3][1]), 14th([3][2]) element corresponds to
// the translation.
//

inline void Identity(value::matrix2d *mat) {
  memset(mat->m, 0, sizeof(value::matrix2d));
  for (size_t i = 0; i < 2; i++) {
    mat->m[i][i] = static_cast<double>(1);
  }
}

inline void Identity(value::matrix3d *mat) {
  memset(mat->m, 0, sizeof(value::matrix3d));
  for (size_t i = 0; i < 3; i++) {
    mat->m[i][i] = static_cast<double>(1);
  }
}

inline void Identity(value::matrix4d *mat) {
  memset(mat->m, 0, sizeof(value::matrix4d));
  for (size_t i = 0; i < 4; i++) {
    mat->m[i][i] = static_cast<double>(1);
  }
}

// forward decl
class MaterialBinding;
struct Model;
class Prim;
class PrimSpec;
struct VariantSet;

// Include model-scope after forward declarations
} // namespace tinyusdz

#include "core/model-scope.hh"

namespace tinyusdz {

// Include variant-types which depends on forward declarations
} // namespace tinyusdz

#include "core/variant-types.hh"

namespace tinyusdz {

///
/// Get elementName from Prim(e.g., Xform::name, GeomMesh::name)
/// `v` must be the value of Prim class.
///
nonstd::optional<std::string> GetPrimElementName(const value::Value &v);

///
/// Set name for Prim `v`(e.g. Xform::name = elementName)
/// `v` must be the value of Prim class.
///
bool SetPrimElementName(value::Value &v, const std::string &elementName);

//
// For `Stage` scene graph.
// Its a freezed state of an element of a scene graph(so no Prim
// additin/deletion from a scene graph is considered). May be Similar to `Prim`
// in pxrUSD. If you want to manipulate scene graph, use PrimSpec instead(but
// PrimSpec is W.I.P.) This class uses tree-representation of `Prim`. Easy to
// use, but may not be performant than flattened array index representation of
// Prim tree(Index-based scene graph such like glTF).
//
class Prim {
 public:
  // elementName is read from `rhs`(if it is a class of Prim)
  Prim(const value::Value &rhs);
  Prim(value::Value &&rhs);

  Prim(const std::string &elementName, const value::Value &rhs);
  Prim(const std::string &elementName, value::Value &&rhs);

  template <typename T>
  Prim(const T &prim) {
    set_primdata(prim);
  }

  template <typename T>
  Prim(const std::string &elementName, const T &prim) {
    set_primdata(elementName, prim);
  }

  // Default constructor (creates empty/invalid Prim)
  Prim() = default;

  // Special member functions (copy and move)
  Prim(const Prim&) = default;
  Prim& operator=(const Prim&) = default;
  Prim(Prim&&) noexcept = default;
  Prim& operator=(Prim&&) noexcept = default;

  // Replace exting prim
  template <typename T>
  void set_primdata(const T &prim) {
    // Check if T is Prim class type.
    static_assert((value::TypeId::TYPE_ID_MODEL_BEGIN <=
                   value::TypeTraits<T>::type_id()) &&
                      (value::TypeId::TYPE_ID_MODEL_END >
                       value::TypeTraits<T>::type_id()),
                  "T is not a Prim class type");
    _data = prim;
    // Use prim.name for elementName
    _elementPath = Path(prim.name, "");
  }

  // Replace exting prim
  template <typename T>
  void set_primdata(const std::string &elementName, const T &prim) {
    // Check if T is Prim class type.
    static_assert((value::TypeId::TYPE_ID_MODEL_BEGIN <=
                   value::TypeTraits<T>::type_id()) &&
                      (value::TypeId::TYPE_ID_MODEL_END >
                       value::TypeTraits<T>::type_id()),
                  "T is not a Prim class type");
    _data = prim;
    SetPrimElementName(_data, elementName);
    _elementPath = Path(elementName, "");
  }

  ///
  /// Add Prim as a child.
  /// When `rename_element_name` is true, rename input Prims elementName to make
  /// it unique among children(since USD(Crate) spec doesn't allow same Prim
  /// elementName in the same Prim hierarchy.
  ///
  /// Renaming rule is Maya-like:
  /// - No elementName given: `default`
  /// - Add or increment number suffix to the elementName:
  ///    - `plane` => `plane1`
  ///    - `plane1` => `plane2`
  ///
  /// Note: This function is thread-safe.
  ///
  /// @return true Upon success. false when failed(e.g. Prim with same
  /// Prim::element_name() already exists when `rename_element_name` is false)
  /// and fill `err` with error message
  ///
  bool add_child(Prim &&prim, const bool rename_element_name = true,
                 std::string *err = nullptr);

  ///
  /// Replace existing child Prim whose elementName is `child_prim_name`.
  /// When there is no child Prim with elementName `child_prim_name` exists,
  /// `prim` is added and rename is elementName to `child_prim_name`.
  ///
  /// @return true Upon success. false when failed(e.g. `child_prim_name` is
  /// empty string or invalid Prim name) and fill `err` with error message.
  ///
  bool replace_child(const std::string &child_prim_name, Prim &&prim,
                     std::string *err = nullptr);

  // TODO: Deprecate this API to disallow direct modification of children.
  std::vector<Prim> &children() { return _children; }

  const std::vector<Prim> &children() const { return _children; }

  const value::Value &data() const { return _data; }
  value::Value &get_data() { return _data; }

  Specifier &specifier() { return _specifier; }

  Specifier specifier() const { return _specifier; }

  // local_path is reserved for Prim composition.
  // for a while please use absolute_path(full Prim absolute path) or
  // element_name(leaf Prim name).
  Path &local_path() { return _path; }
  const Path &local_path() const { return _path; }

  ///
  /// Absolute Prim Path(e.g. "/xform/mesh0") is available after
  /// Stage::compute_absolute_path() or assign it manually by an app.
  ///
  Path &absolute_path() { return _abs_path; }
  const Path &absolute_path() const { return _abs_path; }

  Path &element_path() { return _elementPath; }
  const Path &element_path() const { return _elementPath; }

  // elementName = element_path's prim part
  const std::string &element_name() const { return _elementPath.prim_part(); }

  const std::string type_name() const { return _data.type_name(); }

  uint32_t type_id() const { return _data.type_id(); }

  std::string &prim_type_name() { return _prim_type_name; }
  const std::string &prim_type_name() const { return _prim_type_name; }

  template <typename T>
  bool is() const {
    return (_data.type_id() == value::TypeTraits<T>::type_id());
  }

  // Return a pointer of a concrete Prim class(Xform, Material, ...)
  // Return nullptr when failed to cast or T is not a Prim type.
  template <typename T>
  const T *as() const {
    // Check if T is Prim type. e.g. Xform, Material, ...
    if ((value::TypeId::TYPE_ID_MODEL_BEGIN <=
         value::TypeTraits<T>::type_id()) &&
        (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id())) {
      return _data.as<T>();
    }

    return nullptr;
  }

  const PrimMeta &metas() const;
  PrimMeta &metas();

  int64_t prim_id() const { return _prim_id; }

  int64_t &prim_id() { return _prim_id; }

  const std::map<std::string, VariantSet> &variantSets() const {
    return _variantSets;
  }

  std::map<std::string, VariantSet> &variantSets() { return _variantSets; }

  ///
  /// Get indices for children().
  ///
  /// This is an utility API to traverse child Prims according to `primChildren`
  /// Prim metadata. If you want to traverse child Prims as done in pxrUSD(which
  /// used `primChildren` to determine the order of traversal), use this
  /// function.
  ///
  /// If no `primChildren` Prim metadata, it will simply returns [0,
  /// children().size()) sequence.
  ///
  /// index may have -1, which means invalid(child Prim not found described in
  /// by primChildren) Also, app should extra check of the value of index if
  /// `indices_is_valid` is set to false(index may be duplicated(Duplicated Prim
  /// name exits in `primChildren`)  and not in range `[0, children() -1`)
  ///
  /// NOTE: This function build a cache.
  ///
  /// @param[in] force_update Always rebuild child_indices. false = use cache if
  /// exits.
  /// @param[out] indices_is_valid Optional. Set true when returned indices are
  /// valid.
  ///
  const std::vector<int64_t> &get_child_indices_from_primChildren(
      bool force_update = true, bool *indices_is_valid = nullptr) const;

  // TODO: Add API to get parent Prim directly?
  // (Currently we need to traverse parent Prim using Stage)

 private:
  Path _abs_path;  // Absolute Prim path in a freezed(after composition state).
                   // Usually set by Stage::compute_absolute_path()
  Path _path;  // Prim's local path name. May contain Property, Relationship and
               // other infos, but do not include parent's path. To get fully
               // absolute path of a Prim(e.g. "/xform0/mymesh0", You need to
               // traverse Prim tree and concatename `elementPath` or use
               // ***(T.B.D>) method in `Stage` class
  Path _elementPath;  // leaf("terminal") Prim name.(e.g. "myxform" for `def
                      // Xform "myform"`). For root node, elementPath name is
                      // empty string("").

  std::string _prim_type_name;  // Prim's type name. e.g. "Xform", "Mesh",
                                // "UnknownPrim", ... Could be empty for `def
                                // "myprim" {}`

  Specifier _specifier{
      Specifier::Invalid};  // `def`, `over` or `class`. Usually `def`
  value::Value
      _data;  // Generic container for concrete Prim object. GPrim, Xform, ...

  std::vector<Prim> _children;  // child Prim nodes
  // std::set<std::string> _childrenNames; // child Prim name(elementName).
  std::multiset<std::string>
      _childrenNameSet;  // Stores input child Prim's elementName to assign
                         // unique elementName in `add_child`

  mutable bool _child_dirty{false};
  mutable bool _primChildrenIndicesIsValid{
      false};  // true when indices in _primChildrenIndices are not -1, unique,
               // and index value are within [0, children().size()), and also
               // _primChildrenIndices.size() == children().size()
  mutable std::vector<int64_t>
      _primChildrenIndices;  // Get corresponding array index in _children,
                             // based on `metas().primChildren` token[] info. -1
                             // = invalid.

  int64_t _prim_id{
      -1};  // Unique Prim id when positive(starts with 1). Id is assigned by
            // Stage::compute_absolute_prim_path_and_assign_prim_id. Usually [1,
            // NumPrimsInStage)

  std::map<std::string, VariantSet> _variantSets;
};

bool IsXformablePrim(const Prim &prim);

// forward decl(xform.hh)
struct Xformable;
bool CastToXformable(const Prim &prim, const Xformable **xformable);

///
/// Get Prim's local transform(xformOps) at specified time.
/// For non-Xformable Prim it returns identity matrix.
///
/// @param[in] prim Prim
/// @param[out] resetXformStack Whether Prim's xformOps contains
/// `!resetXformStack!` or not
/// @param[in] t time
/// @param[in] tinterp Interpolation type(Linear or Held)
///
value::matrix4d GetLocalTransform(const Prim &prim, bool *resetXformStak,
                                  double t = value::TimeCode::Default(),
                                  value::TimeSampleInterpolationType tinterp =
                                      value::TimeSampleInterpolationType::Linear);

///
/// TODO: Deprecate this class and use PrimPec
/// NOTE PrimNode is designed for Stage(freezed)
///
/// Contains concrete Prim object and composition elements.
///
/// PrimNode is near to the final state of `Prim`.
/// Doing one further step(Composition, Flatten, select Variant) to get `Prim`.
///
/// Similar to `PrimIndex` in pxrUSD
///

class PrimNode {
  Path path;
  Path elementPath;

  PrimNode(const value::Value &rhs);

  PrimNode(value::Value &&rhs);

  value::Value prim;  // GPrim, Xform, ...

  std::vector<PrimNode> children;  // child nodes

  ///
  /// Select variant.
  ///
  bool select_variant(const std::string &target_name,
                      const std::string &variant_name) {
    const auto m = _vsmap.find(target_name);
    if (m != _vsmap.end()) {
      _current_vsmap[target_name] = variant_name;
      return true;
    } else {
      return false;
    }
  }

  ///
  /// Get current variant selection.
  ///
  bool current_variant_selection(const std::string &target_name,
                      std::string *selected_variant_name) {

    if (!selected_variant_name) {
      return false;
    }

    const auto m = _vsmap.find(target_name);
    if (m != _vsmap.end()) {
      const auto sm = _current_vsmap.find(target_name);
      if (sm != _current_vsmap.end()) {
        (*selected_variant_name) = sm->second;
      } else {
        (*selected_variant_name) = m->second;
      }
      return true;
    } else {
      return false;
    }
  }

  ///
  /// List variants in this Prim
  ///
  /// key = variant prim name
  /// value = variants
  ///
  const VariantSelectionMap &get_variant_selection_map() const { return _vsmap; }

  ///
  /// Variants
  ///
  /// VariantSet = Prim metas + Properties and/or child Prims
  ///            = repsetent as PrimNode for a while.
  ///
  ///
  /// key = variant name
  using VariantSet = std::map<std::string, PrimNode>;
  std::map<std::string, VariantSet> varitnSetList;  // key = variant

  VariantSelectionMap _vsmap;          // Original variant selections
  VariantSelectionMap _current_vsmap;  // Currently selected variants

  std::vector<value::token> primChildren;  // List of child Prim nodes
  std::vector<value::token> properties;    // List of property names
  std::vector<value::token> variantChildren; // List of child VariantSet nodes.
};

/// Similar to PrimSpec
/// PrimSpec is a Prim object state just after reading it from USDA and USDC.
/// The state before compositions and Prim reconstruction by applying
/// schema(ReconstructPrim in prim-reconstruct.hh) happens.
///
/// Its composed primarily of name, specifier, PrimMeta and
/// Properties(Relationships and Attributes)
class PrimSpec {
 public:
  PrimSpec() {
    //TUSDZ_LOG_I("PrimSpec default constructor called");
  }

  PrimSpec(const Specifier &spec, const std::string &name)
      : _specifier(spec), _name(name) {
    //TUSDZ_LOG_I("PrimSpec constructor called with spec and name: " << name);
  }
  PrimSpec(const Specifier &spec, const std::string &typeName,
           const std::string &name)
      : _specifier(spec), _typeName(typeName), _name(name) {
    //TUSDZ_LOG_I("PrimSpec constructor called with spec, typeName, and name: " << name);
  }

  PrimSpec(const PrimSpec &rhs) {
    //TUSDZ_LOG_I("PrimSpec copy constructor called");
    if (this != &rhs) {
      CopyFrom(rhs);
    }
  }

  PrimSpec &operator=(const PrimSpec &rhs) {
    //TUSDZ_LOG_I("PrimSpec copy assignment operator called");
    if (this != &rhs) {
      CopyFrom(rhs);
    }

    return *this;
  }

  PrimSpec(PrimSpec &&rhs) noexcept {
    //TUSDZ_LOG_I("PrimSpec move constructor called");
    if (this != &rhs) {
      MoveFrom(rhs);
    }
  }

  PrimSpec &operator=(PrimSpec &&rhs) noexcept {
    //TUSDZ_LOG_I("PrimSpec move assignment operator called");
    if (this != &rhs) {
      MoveFrom(rhs);
    }

    return *this;
  }

  const std::string &name() const { return _name; }
  std::string &name() { return _name; }

  const std::string &typeName() const { return _typeName; }
  // Can change type name
  std::string &typeName() { return _typeName; }

  const Specifier &specifier() const { return _specifier; }
  Specifier &specifier() { return _specifier; }

  const std::vector<PrimSpec> &children() const { return _children; }
  std::vector<PrimSpec> &children() { return _children; }

  ///
  /// Select variant.
  ///
  bool select_variant(const std::string &target_name,
                      const std::string &variant_name) {
    if (metas().variants.has_value()) {
      const auto m = metas().variants.value().find(target_name);
      if (m != metas().variants.value().end()) {
        _current_vsmap[target_name] = variant_name;
        return true;
      } else {
        return false;
      }
    }
    return false;
  }

  bool current_variant_selection(const std::string &target_name,
                      std::string *selected_variant_name) {

    if (!selected_variant_name) {
      return false;
    }

    if (!metas().variants.has_value()) {
      return false;
    }

    const auto &vsmap = metas().variants.value();

    const auto m = vsmap.find(target_name);
    if (m != vsmap.end()) {
      const auto sm = _current_vsmap.find(target_name);
      if (sm != _current_vsmap.end()) {
        (*selected_variant_name) = sm->second;
      } else {
        (*selected_variant_name) = m->second;
      }
      return true;
    } else {
      return false;
    }
  }

  ///
  /// List variants in this PrimSpec
  /// key = variant name
  /// value = variats
  ///
  const VariantSelectionMap get_variant_selection_map() const {
    VariantSelectionMap vsmap;
    if (metas().variants.has_value()) {
      vsmap = metas().variants.value();
    }
    return vsmap;
  }

  ///
  /// Variants
  ///
  /// VariantSet = Prim metas + Properties and/or child Prims
  ///            = repsetent as PrimNode for a while.
  ///
  ///
  /// key = variant name
  std::map<std::string, VariantSetSpec> &variantSets() { return _variantSets; }
  const std::map<std::string, VariantSetSpec> &variantSets() const { return _variantSets; }

  const PrimMeta &metas() const { return _metas; }

  PrimMeta &metas() { return _metas; }

  using PropertyMap = std::map<std::string, Property>;

  const PropertyMap &props() const { return _props; }
  PropertyMap &props() { return _props; }

  const std::vector<Reference> &get_references();
  const ListEditQual &get_references_listedit_qualifier();

  const std::vector<Payload> &get_payloads();
  const ListEditQual &get_payloads_listedit_qualifier();

  const std::vector<value::token> &primChildren() const {
    return _primChildren;
  }

  const std::vector<value::token> &propertyNames() const {
    return _properties;
  }

  const std::string &get_current_working_path() const {
    return _current_working_path;
  }

  const std::vector<std::string> &get_asset_search_paths() const {
    return _asset_search_paths;
  }

  void set_current_working_path(const std::string &s) {
    _current_working_path = s;
  }

  void set_asset_search_paths(const std::vector<std::string> &search_paths) {
    _asset_search_paths = search_paths;
  }

  void set_asset_resolution_state(
    const std::string &cwp, const std::vector<std::string> &search_paths) {
    _current_working_path = cwp;
    _asset_search_paths = search_paths;
  }

 private:
  void CopyFrom(const PrimSpec &rhs) {
    _specifier = rhs._specifier;
    _typeName = rhs._typeName;
    _name = rhs._name;

    _children = rhs._children;

    _props = rhs._props;

    //_vsmap = rhs._vsmap;
    _current_vsmap = rhs._current_vsmap;

    _variantSets = rhs._variantSets;

    _primChildren = rhs._primChildren;
    _properties = rhs._properties;
    _variantChildren = rhs._variantChildren;

    _metas = rhs._metas;

    _current_working_path = rhs._current_working_path;
    _asset_search_paths = rhs._asset_search_paths;
  }

  void MoveFrom(PrimSpec &rhs) {
    _specifier = std::move(rhs._specifier);
    _typeName = std::move(rhs._typeName);
    _name = std::move(rhs._name);

    _children = std::move(rhs._children);

    _props = std::move(rhs._props);

    //_vsmap = std::move(rhs._vsmap);
    _current_vsmap = std::move(rhs._current_vsmap);

    _variantSets = std::move(rhs._variantSets);

    _primChildren = std::move(rhs._primChildren);
    _properties = std::move(rhs._properties);
    _variantChildren = std::move(rhs._variantChildren);

    _metas = std::move(rhs._metas);

    _current_working_path = rhs._current_working_path;
    _asset_search_paths = std::move(rhs._asset_search_paths);
  }

  Specifier _specifier{Specifier::Def};
  std::string _typeName;  // prim's typeName(e.g. "Xform", "Material") This is
                          // identitical to `typeName` in Crate format)
  std::string _name;      // elementName. Should not be empty.

  std::vector<PrimSpec> _children;  // child nodes

  PropertyMap _props;

  ///
  /// Variants
  ///
  /// variant element = Property or Prim
  ///
  using PrimSpecMap = std::map<std::string, PrimSpec>;

  //VariantSelectionMap _vsmap;  // Original variant selections
  VariantSelectionMap _current_vsmap;  // Currently selected variants

  std::map<std::string, VariantSetSpec> _variantSets;

  std::vector<value::token> _primChildren;  // List of child PrimSpec nodes
  std::vector<value::token> _properties;    // List of property names
  std::vector<value::token> _variantChildren;

  PrimMeta _metas;

  ///
  /// For solving asset path in nested composition.
  /// Keep asset resolution state.
  /// TODO: Use struct. Store userdata pointer.
  ///
  std::string _current_working_path;
  std::vector<std::string> _asset_search_paths;

};

namespace value {

#include "define-type-trait.inc"

DEFINE_TYPE_TRAIT(Reference, "ref", TYPE_ID_REFERENCE, 1);
DEFINE_TYPE_TRAIT(Specifier, "specifier", TYPE_ID_SPECIFIER, 1);
DEFINE_TYPE_TRAIT(Permission, "permission", TYPE_ID_PERMISSION, 1);
DEFINE_TYPE_TRAIT(Variability, "variability", TYPE_ID_VARIABILITY, 1);

DEFINE_TYPE_TRAIT(VariantSelectionMap, "variants", TYPE_ID_VARIANT_SELECION_MAP,
                  0);

DEFINE_TYPE_TRAIT(Payload, "payload", TYPE_ID_PAYLOAD, 1);
DEFINE_TYPE_TRAIT(LayerOffset, "LayerOffset", TYPE_ID_LAYER_OFFSET, 1);

DEFINE_TYPE_TRAIT(ListOp<value::token>, "ListOpToken", TYPE_ID_LIST_OP_TOKEN,
                  1);
DEFINE_TYPE_TRAIT(ListOp<std::string>, "ListOpString", TYPE_ID_LIST_OP_STRING,
                  1);
DEFINE_TYPE_TRAIT(ListOp<Path>, "ListOpPath", TYPE_ID_LIST_OP_PATH, 1);
DEFINE_TYPE_TRAIT(ListOp<Reference>, "ListOpReference",
                  TYPE_ID_LIST_OP_REFERENCE, 1);
DEFINE_TYPE_TRAIT(ListOp<int32_t>, "ListOpInt", TYPE_ID_LIST_OP_INT, 1);
DEFINE_TYPE_TRAIT(ListOp<uint32_t>, "ListOpUInt", TYPE_ID_LIST_OP_UINT, 1);
DEFINE_TYPE_TRAIT(ListOp<int64_t>, "ListOpInt64", TYPE_ID_LIST_OP_INT64, 1);
DEFINE_TYPE_TRAIT(ListOp<uint64_t>, "ListOpUInt64", TYPE_ID_LIST_OP_UINT64, 1);
DEFINE_TYPE_TRAIT(ListOp<Payload>, "ListOpPayload", TYPE_ID_LIST_OP_PAYLOAD, 1);

DEFINE_TYPE_TRAIT(Path, "Path", TYPE_ID_PATH, 1);
DEFINE_TYPE_TRAIT(Relationship, "Relationship", TYPE_ID_RELATIONSHIP, 1);
// TODO(syoyo): Define as 1D array?
DEFINE_TYPE_TRAIT(std::vector<Path>, "PathVector", TYPE_ID_PATH_VECTOR, 1);

DEFINE_TYPE_TRAIT(std::vector<value::token>, "token[]", TYPE_ID_TOKEN_VECTOR,
                  1);

DEFINE_TYPE_TRAIT(value::TimeSamples, "TimeSamples", TYPE_ID_TIMESAMPLES, 1);

DEFINE_TYPE_TRAIT(Collection, "Collection", TYPE_ID_COLLECTION, 1);
DEFINE_TYPE_TRAIT(CollectionInstance, "CollectionInstance", TYPE_ID_COLLECTION_INSTANCE, 1);
DEFINE_TYPE_TRAIT(ColorSpaceAPI, "ColorSpaceAPI", TYPE_ID_COLOR_SPACE_API, 1);

DEFINE_TYPE_TRAIT(Model, "Model", TYPE_ID_MODEL, 1);
DEFINE_TYPE_TRAIT(Scope, "Scope", TYPE_ID_SCOPE, 1);

DEFINE_TYPE_TRAIT(CustomDataType, "customData", TYPE_ID_CUSTOMDATA,
                  1);  // TODO: Unify with `dict`?

DEFINE_TYPE_TRAIT(Extent, "float3[]", TYPE_ID_EXTENT, 2);  // float3[2]

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

namespace prim {

using PropertyMap = std::map<std::string, Property>;
// Single listop+items pair (used internally and in printing)
using ReferenceListOp = std::pair<ListEditQual, std::vector<Reference>>;
using PayloadListOp = std::pair<ListEditQual, std::vector<Payload>>;
// Full list supporting multiple listops
using ReferenceList = std::vector<ReferenceListOp>;
using PayloadList = std::vector<PayloadListOp>;

}  // namespace prim


// TODO(syoyo): Range, Interval, Rect2i, Frustum, MultiInterval
// and Quaternion?

/*
#define VT_GFRANGE_VALUE_TYPES                 \
((      GfRange3f,           Range3f        )) \
((      GfRange3d,           Range3d        )) \
((      GfRange2f,           Range2f        )) \
((      GfRange2d,           Range2d        )) \
((      GfRange1f,           Range1f        )) \
((      GfRange1d,           Range1d        ))

#define VT_RANGE_VALUE_TYPES                   \
    VT_GFRANGE_VALUE_TYPES                     \
((      GfInterval,          Interval       )) \
((      GfRect2i,            Rect2i         ))

#define VT_QUATERNION_VALUE_TYPES           \
((      GfQuaternion,        Quaternion ))

#define VT_NONARRAY_VALUE_TYPES                 \
((      GfFrustum,           Frustum))          \
((      GfMultiInterval,     MultiInterval))

*/

}  // namespace tinyusdz
