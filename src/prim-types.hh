// SPDX-License-Identifier: Apache 2.0

///
/// @file prim-types.hh
/// @brief Core USD primitive type definitions and data structures
///
/// Contains fundamental USD concepts including Prim (primitive), Layer,
/// Properties, Attributes, Relationships, and supporting data structures.
/// These form the building blocks of USD scene graphs.
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

//#define TINYUSDZ_INSIDE_PRIM_TYPES
#include "attribute.hh"
//#undef TINYUSDZ_INSIDE_PRIM_TYPES
#include "api-schemas.hh"
#include "define-type-trait.hh"
#include "dictionary.hh"
#include "enum-types.hh"
#include "list-op.hh"
#include "material-binding.hh"
#include "ordered-dict.hh"
#include "timesamples.hh"

namespace tinyusdz {


// SpecType enum must be same order with pxrUSD's SdfSpecType(since enum value
// is stored in Crate directly)










// Return false when invalid character(e.g. '%') exists in a given string.
// This function only validates `elementName` of a Prim(e.g. "dora", "xform1").
// If you want to validate a Prim path(e.g. "/root/xform1"),
// Use ValidatePrimPath() in path-util.hh
bool ValidatePrimElementName(const std::string &tok);

///
/// Simlar to SdfPath.
/// NOTE: We are doging refactoring of Path class, so the following comment may
/// not be correct.
///
/// We don't need the performance for USDZ, so use naiive implementation
/// to represent Path.
/// Path is something like Unix path, delimited by `/`, ':' and '.'
/// Square brackets('<', '>' is not included)
///
/// Root path is represented as prim path "/" and elementPath ""(empty).
///
/// Example:
///
/// - `/muda/bora.dora` : prim_part is `/muda/bora`, prop_part is `.dora`.
/// - `bora` : Could be Element(leaf) path or Relative path
///
/// ':' is a namespce delimiter(example `input:muda`).
///
/// Limitations:
///
/// - Relational attribute path(`[` `]`. e.g. `/muda/bora[/ari].dora`) is not
/// supported.
/// - Variant chars('{' '}') is not supported(yet).
/// - Relative path(e.g. '../') is not yet supported(TODO)
///
/// and have more limitatons.
///
class Path {
 public:
  // Similar to SdfPathNode
  enum class PathType {
    Prim,
    PrimProperty,
    RelationalAttribute,
    MapperArg,
    Target,
    Mapper,
    PrimVariantSelection,
    Expression,
    Root,
  };

  Path() : _valid(false) {}

  static Path make_root_path() {
    Path p = Path("/", "");
    // elementPath is empty for root.
    p._element = "";
    p._valid = true;
    return p;
  }

  // Create Path both from Prim Path and Prop
  // If `prim` starts
  // "/aaa", "bora" => /aaa.bora
  // "/aaa", "" => /aaa (prim only)
  // "", "bora" => .bora (property only)
  //
  // Note: This constructor may fail to extract elementName from given `prim`
  // and `prop`. It is highly recommended to use AppendPrim() and AppendProperty
  // to. construct Path hierarchy(e.g. `/aaa/xform/geom.points`) so that
  // elementName is set correctly.
  Path(const std::string &prim, const std::string &prop);

  // : prim_part(prim), valid(true) {}
  // Path(const std::string &prim, const std::string &prop)
  //    : prim_part(prim), prop_part(prop) {}

  Path(const Path &rhs) = default;

  Path &operator=(const Path &rhs) {
    this->_valid = rhs._valid;

    this->_prim_part = rhs._prim_part;
    this->_prop_part = rhs._prop_part;
    this->_element = rhs._element;

    return (*this);
  }

  std::string full_path_name() const {
    std::string s;
    if (!_valid) {
      s += "#INVALID#";
    }

    s += _prim_part;
    if (_prop_part.empty()) {
      return s;
    }

    s += "." + _prop_part;

    return s;
  }

  const std::string &prim_part() const { return _prim_part; }
  const std::string &prop_part() const { return _prop_part; }

  const std::string &variant_part() const {
    _variant_part_str =
        "{" + _variant_part + "=" + _variant_selection_part + "}";
    return _variant_part_str;
  }

  void set_path_type(const PathType ty) { _path_type = ty; }

  bool get_path_type(PathType &ty) {
    if (_path_type) {
      ty = _path_type.value();
    }
    return false;
  }

  // IsPropertyPath: PrimProperty or RelationalAttribute
  bool is_property_path() const {
    if (_path_type) {
      if ((_path_type.value() == PathType::PrimProperty ||
           (_path_type.value() == PathType::RelationalAttribute))) {
        return true;
      }
    }

    // TODO: RelationalAttribute
    if (_prim_part.empty()) {
      return false;
    }

    if (_prop_part.size()) {
      return true;
    }

    return false;
  }

  // Is Prim path?
  bool is_prim_path() const {
    if (_prop_part.size()) {
      return false;
    }

    if (_prim_part.size()) {
      return true;
    }

    return false;
  }

  // Is Prim's property path?
  // True when both PrimPart and PropPart are not empty.
  bool is_prim_property_path() const {
    if (_prim_part.empty()) {
      return false;
    }
    if (_prop_part.size()) {
      return true;
    }
    return false;
  }

  bool is_valid() const { return _valid; }

  bool is_empty() {
    return (_prim_part.empty() && _variant_part.empty() && _prop_part.empty());
  }

  // static Path RelativePath() { return Path("."); }

  // Append property path(change internal state)
  Path append_property(const std::string &elem);

  // Append prim or variantSelection path(change internal state)
  Path append_element(const std::string &elem);
  Path append_prim(const std::string &elem) {
    return append_element(elem);
  }  // for legacy

  // Const version. Does not change internal state.
  const Path AppendProperty(const std::string &elem) const;
  const Path AppendPrim(const std::string &elem) const;
  const Path AppendElement(const std::string &elem) const;

  // Get element name(the last element of Path. i.e. Prim's name, Property's
  // name)
  const std::string &element_name() const;

  ///
  /// Split a path to the root(common ancestor) and its siblings
  ///
  /// example:
  ///
  /// - / -> [/, Empty]
  /// - /bora -> [/bora, Empty]
  /// - /bora/dora -> [/bora, /dora]
  /// - /bora/dora/muda -> [/bora, /dora/muda]
  /// - bora -> [Empty, bora]
  /// - .muda -> [Empty, .muda]
  ///
  std::pair<Path, Path> split_at_root() const;

  ///
  /// TODO: Deprecate(use get_parent_path() instead)
  ///
  /// Get parent Prim path.
  /// If the given path is a root Prim path(e.g. "/bora"), same Path is
  /// returned.
  ///
  /// example:
  ///
  /// - / -> invalid Path
  /// - /bora -> /bora
  /// - /bora/dora -> /bora
  /// - /bora/dora.prop -> /bora/dora
  /// - dora/bora -> dora
  /// - dora -> invalid Path
  /// - .dora -> invalid Path(path is property path)
  Path get_parent_prim_path() const;

  ///
  /// Get parent Path.
  /// If the given path is the root path("/") same Path is returned.
  ///
  /// example:
  ///
  /// - / -> invalid Path
  /// - /bora -> /
  /// - /bora/dora -> /bora
  /// - /bora/dora.prop -> /bora/dora
  /// - dora/bora -> dora
  /// - dora -> invalid Path
  /// - .dora -> invalid Path(path is property path)
  Path get_parent_path() const;

  ///
  /// Check if this Path has same prefix for given Path
  ///
  /// example.
  /// rhs path: /bora/dora
  ///
  /// /bora/dora/muda -> true
  /// /bora/dora2 -> fase
  ///
  /// If the prefix path contains prop part, compare it with ==
  /// (assume no hierarchy in property part)
  ///
  bool has_prefix(const Path &rhs) const;

  ///
  /// Replace Prim path prefix.
  /// example.
  /// srcPrefix = /bora/dora
  /// dstPrefix = /bora2/dora2
  /// 
  /// /bora/dora/muda -> /bora2/dora2/muda 
  ///
  bool replace_prefix(const Path &srcPrefix, const Path &dstPrefix);

  ///
  /// @returns true if a path is '/' only
  ///
  bool is_root_path() const {
    if (!_valid) {
      return false;
    }

    if ((_prim_part.size() == 1) && (_prim_part[0] == '/')) {
      return true;
    }

    return false;
  }

  ///
  /// @returns true if a path is root prim: e.g. '/bora'
  ///
  bool is_root_prim() const {
    if (!_valid) {
      return false;
    }

    if (is_root_path()) {
      return false;
    }

    if ((_prim_part.size() > 1) && (_prim_part[0] == '/')) {
      // no other '/' except for the fist one
      if (_prim_part.find_last_of('/') == 0) {
        return true;
      }
    }

    return false;
  }

  bool is_absolute_path() const {
    if (_prim_part.size() && _prim_part[0] == '/') {
      return true;
    }

    return false;
  }

  bool is_relative_path() const {
    if (_prim_part.size()) {
      return !is_absolute_path();
    }

    return true;  // prop part only
  }

#if 0 // TODO: rmove
  bool is_variant_selection_path() const {
    if (!is_valid()) {
      return false;
    }

    if (_variant_part.size()) {
      return true;
    }

    return false;
  }
#endif

  // Strip '/'
  Path &make_relative() {
    if (is_absolute_path() && (_prim_part.size() > 1)) {
      // Remove first '/'
      _prim_part.erase(0, 1);
    }
    return *this;
  }

  const Path make_relative(Path &&rhs) {
    (*this) = std::move(rhs);

    return make_relative();
  }

  static const Path make_relative(const Path &rhs) {
    Path p = rhs;  // copy
    return p.make_relative();
  }

  static bool LessThan(const Path &lhs, const Path &rhs);

  // To sort paths lexicographically.
  // TODO: consider abs and relative path correctly
  bool operator<(const Path &rhs) const {
    if (full_path_name() == rhs.full_path_name()) {
      return false;
    }

    if (prim_part().empty() || rhs.prim_part().empty()) {
      return prim_part().empty() && rhs.prim_part().size();
    }

    return LessThan(*this, rhs);
  }

 private:
  void _update(const std::string &p, const std::string &prop);

  std::string _prim_part;     // e.g. /Model/MyMesh, MySphere
  std::string _prop_part;     // e.g. visibility (`.` is not included)
  std::string _variant_part;  // e.g. `variantColor` for {variantColor=green}
  std::string _variant_selection_part;  // e.g. `green` for {variantColor=green}
                                        // . Could be empty({variantColor=}).
  mutable std::string _variant_part_str;  // str buffer for variant_part()
  mutable std::string _element;           // Element name

  nonstd::optional<PathType> _path_type;  // Currently optional.

  bool _valid{false};
};

#if 0
///
/// Split Path by the delimiter(e.g. "/") then create lists.
///
class TokenizedPath {
 public:
  TokenizedPath() {}

  TokenizedPath(const Path &path) {
    std::string s = path.prop_part();
    if (s.empty()) {
      // ???
      return;
    }

    if (s[0] != '/') {
      // Path must start with "/"
      return;
    }

    s.erase(0, 1);

    char delimiter = '/';
    size_t pos{0};
    while ((pos = s.find(delimiter)) != std::string::npos) {
      std::string token = s.substr(0, pos);
      _tokens.push_back(token);
      s.erase(0, pos + sizeof(char));
    }

    if (!s.empty()) {
      // leaf element
      _tokens.push_back(s);
    }
  }

 private:
  std::vector<std::string> _tokens;
};
#endif

bool operator==(const Path &lhs, const Path &rhs);

// variants in Prim Meta.
//
// e.g.
// variants = {
//   string variant0 = "bora"
//   string variant1 = "dora"
// }
// pxrUSD uses dict type for the content, but TinyUSDZ only accepts list of
// strings for now
//
using VariantSelectionMap = std::map<std::string, std::string>;



struct AssetInfo {
  // builtin fields
  value::AssetPath identifier;
  std::string name;
  std::vector<value::AssetPath> payloadAssetDependencies;
  std::string version;

  // Other fields
  Dictionary _fields;
};


// USDZ AR class?
// Preliminary_Trigger,
// Preliminary_PhysicsGravitationalForce,
// Preliminary_InfiniteColliderPlane,
// Preliminary_ReferenceImage,
// Preliminary_Action,
// Preliminary_Text,


// SdfLayerOffset
struct LayerOffset {
  double _offset{0.0};
  double _scale{1.0};
};

// SdfReference
struct Reference {
  value::AssetPath asset_path;
  Path prim_path;
  LayerOffset layerOffset;
  Dictionary customData;
};

// SdfPayload
struct Payload {
  value::AssetPath asset_path;  // std::string in SdfPayload
  Path prim_path;
  LayerOffset layerOffset;  // from 0.8.0
  // No customData for Payload

  // NOTE: pxrUSD encodes `payload = None` as Payload with empty paths in USDC(Crate).
  // (Not ValueBlock)
  bool is_none() const {
    return asset_path.GetAssetPath().empty() && !prim_path.is_valid();
  }
};

// Metadata for Prim
struct PrimMetas {
  nonstd::optional<bool> active;  // 'active'
  nonstd::optional<bool> hidden;  // 'hidden'
  nonstd::optional<Kind> kind;    // 'kind'. user-defined kind value is stored in _kind_str;
  std::string _kind_str;

  nonstd::optional<Dictionary>
      assetInfo;  // 'assetInfo' // TODO: Use AssetInfo?
  nonstd::optional<Dictionary> customData;  // `customData`
  nonstd::optional<value::StringData> doc;  // 'documentation'
  nonstd::optional<value::StringData>
      comment;  // 'comment'  (String only metadata value)
  nonstd::optional<APISchemas> apiSchemas;  // 'apiSchemas'
  nonstd::optional<Dictionary>
      sdrMetadata;  // 'sdrMetadata' (usdShade Prim only?)

  nonstd::optional<bool> instanceable; // 'instanceable'
  nonstd::optional<Dictionary> clips; // 'clips'

  // String representation of Kind.
  // For user-defined Kind, it returns `_kind_str`
  const std::string get_kind() const;

  //
  // AssetInfo utility function
  //
  // Convert CustomDataType to AssetInfo
  AssetInfo get_assetInfo(bool *authored = nullptr) const;

  //
  // Compositions
  //
  nonstd::optional<std::pair<ListEditQual, std::vector<Reference>>> references;
  nonstd::optional<std::pair<ListEditQual, std::vector<Payload>>>
      payload;  // NOTE: not `payloads`
  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>>
      inherits;  // 'inherits'
  nonstd::optional<std::pair<ListEditQual, std::vector<std::string>>>
      variantSets;  // 'variantSets'. Could be `token` but treat as
                    // `string`(Crate format uses `string`)

  nonstd::optional<VariantSelectionMap> variants;  // `variants`

  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>>
      specializes;  // 'specializes'

  // USDZ extensions
  nonstd::optional<std::string> sceneName;  // 'sceneName'

  // Omniverse extensions(TODO: Use UTF8 string type?)
  // https://github.com/PixarAnimationStudios/USD/pull/2055
  nonstd::optional<std::string> displayName;  // 'displayName'

  // Unregistered metadatum. value is represented as string.
  std::map<std::string, std::string> unregisteredMetas;

  Dictionary meta;  // other non-buitin meta values. TODO: remove this variable
                    // and use `customData` instead, since pxrUSD does not allow
                    // non-builtin Prim metadatum

  ///
  /// Update metadatum with rhs(authored metadataum only)
  ///
  /// @param[in] override_authored true: override this.metadataum(authored or not-authored) when rhs.metadatum is authoerd, false override only when this.metadatum is not authored and rhs.metadataum is authored.
  ///
  void update_from(const PrimMetas &rhs, bool override_authored = true);


#if 0
  // String only metadataum.
  // TODO: Represent as `MetaVariable`?
  std::vector<value::StringData> stringData;
#endif

  // FIXME: Find a better way to detect Prim meta is authored...
  bool authored() const {
    return (active || hidden || kind || customData || references || payload ||
            inherits || variants || variantSets || specializes || displayName ||
            sceneName || doc || comment || unregisteredMetas.size() || meta.size() || apiSchemas ||
            sdrMetadata || assetInfo || instanceable || clips);
  }

  //
  // Infos used indirectly.
  //

  // Used to display/traverse Prim items based on this array
  // USDA: By appearance. USDC: "primChildren" TokenVector field
  std::vector<value::token> primChildren;

  // Used to display/traverse Property items based on this array
  // USDA: By appearance. USDC: "properties" TokenVector field
  std::vector<value::token> properties;

  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>> inheritPaths;

  nonstd::optional<std::vector<value::token>> variantChildren;
  nonstd::optional<std::vector<value::token>> variantSetChildren;
};

// For backward compatibility
using PrimMeta = PrimMetas;

#if 0
// Metadata for Property(Relationship and Attribute)
// TODO: Rename to PropMetas
struct AttrMetas {
  // frequently used items
  // nullopt = not specified in USD data
  nonstd::optional<Interpolation> interpolation;  // 'interpolation'
  nonstd::optional<uint32_t> elementSize;         // usdSkel 'elementSize'
  nonstd::optional<bool> hidden;                  // 'hidden'
  nonstd::optional<value::StringData> comment;    // `comment`
  nonstd::optional<Dictionary> customData;        // `customData`

  nonstd::optional<double> weight;  // usdSkel inbetween BlendShape weight.

  // usdShade
  nonstd::optional<value::token> connectability; // NOTE: applies to attr
  nonstd::optional<value::token> outputName; // NOTE: applies to rel
  nonstd::optional<value::token> renderType; // NOTE: applies to prop
  nonstd::optional<Dictionary> sdrMetadata; // NOTE: applies to attr(also seen in prim meta)

  nonstd::optional<std::string> displayName;  // 'displayName'
  nonstd::optional<std::string> displayGroup;  // 'displayGroup'


  //
  // MaterialBinding
  //
  // Could be arbitrary token value so use `token[]` type.
  // For now, either `weakerThanDescendants` or `strongerThanDescendants` are
  // valid token.
  nonstd::optional<value::token> bindMaterialAs;  // 'bindMaterialAs' NOTE: applies to rel.

  std::map<std::string, MetaVariable> meta;  // other meta values

  // String only metadataum.
  // TODO: Represent as `MetaVariable`?
  std::vector<value::StringData> stringData;


  //
  // Some handy methods for non-frequently used metadatum.
  //
  bool has_colorSpace() const;
  value::token get_colorSpace() const; // return empty when not authored or 'colorSpace' metadataum is not token type.

  bool has_unauthoredValuesIndex() const;
  int get_unauthoredValuesIndex() const; // return -1 when not authored or 'unauthoredValuesIndex' metadataum is not int type.

  bool authored() const {
    return (interpolation || elementSize || hidden || customData || weight ||
            connectability || outputName || renderType || sdrMetadata || displayName || displayGroup || bindMaterialAs || meta.size() || stringData.size());
  }
};

// For backward compatibility
using AttrMeta = AttrMetas;

using PropMetas = AttrMetas;
#endif

// TODO: Move to value-types.hh?
//
// Typed TimeSamples value
//
// double radius.timeSamples = { 0: 1.0, 1: None, 2: 3.0 }
//
// in .usd, are represented as
//
// 0: (1.0, false)
// 1: (2.0, true)
// 2: (3.0, false)
//



///
/// Tyeped Attribute without fallback(default) value.
/// For attribute with `uniform` qualifier or TimeSamples, or have
/// `.connect`(Connection)
///
/// To support multiple definition of attribute(up to 2), we support both having
/// Connection and values.
///
/// e.g.  float var = 1.0
///       float var.connect = </path/to/value>
///       (metadata is shared)
///


 
bool ConvertTokenAttributeToStringAttribute(
      const TypedAttribute<Animatable<value::token>> &inp,
      TypedAttribute<Animatable<std::string>> &out);


///
/// Similar to pxrUSD's PrimIndex
///
class PrimNode;

#if 0  // TODO
class PrimRange
{
 public:
  class iterator;

  iterator begin() const {
  }
  iterator end() const {
  }

 private:
  const PrimNode *begin_;
  const PrimNode *end_;
  size_t depth_{0};
};
#endif


//
// Colum-major order(e.g. employed in OpenGL).
// For example, 12th([3][0]), 13th([3][1]), 14th([3][2]) element corresponds to
// the translation.
//
// template <typename T, size_t N>
// struct Matrix {
//  T m[N][N];
//  constexpr static uint32_t n = N;
//};

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

struct Extent {
  value::float3 lower{{std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity()}};

  value::float3 upper{{-std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity()}};

  Extent() = default;

  Extent(const value::float3 &l, const value::float3 &u) : lower(l), upper(u) {}

  bool is_valid() const {
    if (lower[0] > upper[0]) return false;
    if (lower[1] > upper[1]) return false;
    if (lower[2] > upper[2]) return false;

    return std::isfinite(lower[0]) && std::isfinite(lower[1]) &&
           std::isfinite(lower[2]) && std::isfinite(upper[0]) &&
           std::isfinite(upper[1]) && std::isfinite(upper[2]);
  }

  std::array<std::array<float, 3>, 2> to_array() const {
    std::array<std::array<float, 3>, 2> ret;
    ret[0][0] = lower[0];
    ret[0][1] = lower[1];
    ret[0][2] = lower[2];
    ret[1][0] = upper[0];
    ret[1][1] = upper[1];
    ret[1][2] = upper[2];

    return ret;
  }

  const Extent &union_with(const value::float3 &p) {
    lower[0] = (std::min)(lower[0], p[0]);
    lower[1] = (std::min)(lower[1], p[1]);
    lower[2] = (std::min)(lower[2], p[2]);

    upper[0] = (std::max)(upper[0], p[0]);
    upper[1] = (std::max)(upper[1], p[1]);
    upper[2] = (std::max)(upper[2], p[2]);

    return *this;
  }

  const Extent &union_with(const value::point3f &p) {
    union_with(value::float3{p.x, p.y, p.z});

    return *this;
  }

  const Extent &union_with(const Extent &box) {
    lower[0] = (std::min)(lower[0], box.lower[0]);
    lower[1] = (std::min)(lower[1], box.lower[1]);
    lower[2] = (std::min)(lower[2], box.lower[2]);

    upper[0] = (std::max)(upper[0], box.upper[0]);
    upper[1] = (std::max)(upper[1], box.upper[1]);
    upper[2] = (std::max)(upper[2], box.upper[2]);

    return *this;
  }
};

#if 0
struct ConnectionPath {
  bool is_input{false};  // true: Input connection. false: Output connection.

  Path path;  // original Path information in USD

  std::string token;  // token(or string) in USD
  int64_t index{-1};  // corresponding array index(e.g. the array index to
                      // `Scene.shaders`)
};

// struct Connection {
//   int64_t src_index{-1};
//   int64_t dest_index{-1};
// };
//
// using connection_id_map =
//     std::unordered_map<std::pair<std::string, std::string>, Connection>;
#endif

//
// Relationship(typeless property)
//
// Relationship class is now defined in relationship.hh
#define TINYUSDZ_INSIDE_PRIM_TYPES
#include "relationship.hh"
#undef TINYUSDZ_INSIDE_PRIM_TYPES

// RelationshipProperty class is now defined in relationship.hh

//
// TypedConnection is a typed version of Relationship
// example:
//
// token varname.connect = </Material/uv.name>
// float specular.connect = </Material/uv.specular>
// float specular:collection.connect = [</Material/uv.specular>,
// </Material/uv.specular_lod0>]
//
//
template <typename T>
class TypedConnection {
 public:
  using type = typename value::TypeTraits<T>::value_type;

  static std::string type_name() { return value::TypeTraits<T>::type_name(); }

  void set_listedit_qual(ListEditQual q) { _listOpQual = q; }
  ListEditQual get_listedit_qual() const { return _listOpQual; }

  // Define-only: token output:surface
  void set_empty() { _authored = true; }

  void set(const Path &p) {
    _targetPaths.clear();
    _targetPaths.push_back(p);
    _authored = true;
  }

  void set(const std::vector<Path> &pv) {
    _targetPaths = pv;
    _authored = true;
  }

  void set(const value::ValueBlock &v) {
    (void)v;
    _blocked = true;
    _authored = true;
  }

  void set_blocked() {
    _blocked = true;
    _authored = true;
  }

  const std::vector<Path> &get_connections() const { return _targetPaths; }

  bool authored() const { return _authored; }

  bool has_value() const { return _targetPaths.size(); }

  bool is_blocked() const { return _blocked; }

  const AttrMeta &metas() const { return _metas; }
  AttrMeta &metas() { return _metas; }

 private:
  std::vector<Path> _targetPaths;
  bool _authored{false};
  bool _blocked{false};
  AttrMeta _metas;
  ListEditQual _listOpQual{ListEditQual::ResetToExplicit};
};

#if 0  // Moved to value::TimeSampleInterpolationType
// Interpolator for TimeSample data
enum class TimeSampleInterpolation {
  Nearest,  // nearest neighbor
  Linear,   // lerp
  // TODO: more to support...
};
#endif

// Attribute is a struct to hold generic attribute of a property(e.g. primvar)
// of Prim.
// It can have multiple values(default value(or ValueBlock), timeSamples and connection) at once.
//
// TODO: Refactor
// Attribute class is now defined in attribute.hh
//#define TINYUSDZ_INSIDE_PRIM_TYPES
//#include "attribute.hh"
//#undef TINYUSDZ_INSIDE_PRIM_TYPES

// Property class is now defined in property.hh
#define TINYUSDZ_INSIDE_PRIM_TYPES
#include "property.hh"
#undef TINYUSDZ_INSIDE_PRIM_TYPES

// Property class definition is complete in property.hh
struct XformOp {
  enum class OpType {
    // matrix
    Transform,

    // vector3
    Translate,
    Scale,

    // scalar
    RotateX,
    RotateY,
    RotateZ,

    // vector3
    RotateXYZ,
    RotateXZY,
    RotateYXZ,
    RotateYZX,
    RotateZXY,
    RotateZYX,

    // quaternion
    Orient,

    // Special token
    ResetXformStack,  // !resetXformStack!
  };

  // OpType op;
  OpType op_type;
  bool inverted{false};  // true when `!inverted!` prefix
  std::string
      suffix;  // may contain nested namespaces. e.g. suffix will be
               // ":blender:pivot" for "xformOp:translate:blender:pivot". Suffix
               // will be empty for "xformOp:translate"

  primvar::PrimVar _var;
  // const value::TimeSamples &get_ts() const { return _var.ts_raw(); }

  std::string get_value_type_name() const { return _var.type_name(); }

  uint32_t get_value_type_id() const { return _var.type_id(); }

  // TODO: Check if T is valid type.
  template <class T>
  void set_value(const T &v) {
    _var.set_value(v);
  }

  template <class T>
  void set_default(const T &v) {
    _var.set_value(v);
  }

  template <class T>
  void set_timesample(const float t, const T &v) {
    _var.set_timesample(t, v);
  }

  void set_timesamples(const value::TimeSamples &v) { _var.set_timesamples(v); }

  void set_timesamples(value::TimeSamples &&v) { _var.set_timesamples(v); }

  bool is_timesamples() const { return _var.is_timesamples(); }
  bool has_timesamples() const { return _var.has_timesamples(); }

  void set_blocked(bool onoff) { _is_blocked = onoff; }
  void clear_blocked() { _is_blocked = false; }

  // check if 'default' value is ValueBlock.
  bool is_blocked() const { return _is_blocked || _var.is_blocked(); }

  bool is_default() const { return _var.is_scalar(); }
  bool has_default() const { return _var.has_default(); }

  nonstd::optional<value::TimeSamples> get_timesamples() const {
    if (has_timesamples()) {
      return _var.ts_raw();
    }
    return nonstd::nullopt;
  }

  nonstd::optional<value::Value> get_scalar() const {
    if (has_default()) {
      return _var.value_raw();
    }
    return nonstd::nullopt;
  }

  nonstd::optional<value::Value> get_default() const {
    return get_scalar();
  }

  template <class T>
  nonstd::optional<T> get_value(double t = value::TimeCode::Default(), 
          value::TimeSampleInterpolationType interp =
               value::TimeSampleInterpolationType::Linear) const {
    if (is_timesamples()) {
      T value{};
      if (get_interpolated_value(&value, t, interp)) {
        return value;
      }
      return nonstd::nullopt;
    }

    return _var.get_value<T>();
  }

  template <class T>
  bool get_interpolated_value(T *dst, double t = value::TimeCode::Default(),
           value::TimeSampleInterpolationType interp =
               value::TimeSampleInterpolationType::Linear) const {
    return _var.get_interpolated_value<T>(t, interp, dst);
  }

  const primvar::PrimVar &get_var() const { return _var; }

  primvar::PrimVar &var() { return _var; }

 private:

  bool _is_blocked{false};
};

// forward decl
struct Model;
class Prim;
class PrimSpec;

// TODO: deprecate this and use PrimSpec for variantSet statement.
// Variant item in VariantSet.
// Variant can contain Prim metas, Prim tree and properties.
struct Variant {
  // const std::string &name() const { return _name; }
  // std::string &name() { return _name; }

  const PrimMeta &metas() const { return _metas; }
  PrimMeta &metas() { return _metas; }

  std::map<std::string, Property> &properties() { return _props; }
  const std::map<std::string, Property> &properties() const { return _props; }

  const std::vector<Prim> &primChildren() const { return _primChildren; }
  std::vector<Prim> &primChildren() { return _primChildren; }

 private:
  // std::vector<int64_t> primIndices;
  std::map<std::string, Property> _props;

  // std::string _name; // variant name
  PrimMeta _metas;

  // We represent Prim children as `Prim` for a while.
  // TODO: Use PrimNode or PrimSpec?
  std::vector<Prim> _primChildren;
};


struct VariantSet {
  // variantSet name = {
  //   "variant1" ...
  //   "variant2" ...
  //   ...
  // }

  std::string name;
  std::map<std::string, Variant> variantSet;
};

// For variantSet statement in PrimSpec(composition).
struct VariantSetSpec
{
  std::string name;
  std::map<std::string, PrimSpec> variantSet;
};

// Collection API
// https://openusd.org/release/api/class_usd_collection_a_p_i.html

constexpr auto kExpandPrims = "expandPrims";
constexpr auto kExplicitOnly = "explicitOnly";
constexpr auto kExpandPrimsAndProperties = "expandPrimsAndProperties";

struct CollectionInstance {

  enum class ExpansionRule {
    ExpandPrims, // "expandPrims" (default)
    ExplicitOnly, // "explicitOnly"
    ExpandPrimsAndProperties, // "expandPrimsAndProperties"
  };

  TypedAttributeWithFallback<ExpansionRule> expansionRule{ExpansionRule::ExpandPrims}; // uniform token collection:collectionName:expansionRule
  TypedAttributeWithFallback<Animatable<bool>> includeRoot{false}; // bool collection:<collectionName>:includeRoot
  nonstd::optional<Relationship> includes; // rel collection:<collectionName>:includes
  nonstd::optional<Relationship> excludes; // rel collection:<collectionName>:excludes

};

class Collection
{
 public:
  const ordered_dict<CollectionInstance> instances() const {
    return _instances;
  }

  bool add_instance(const std::string &name, CollectionInstance &instance) {
    if (_instances.count(name)) {
      return false;
    }

    _instances.insert(name, instance);

    return true;
  }

  bool get_instance(const std::string &name, const CollectionInstance **coll) const {
    if (!coll) {
      return false;
    }

    return _instances.at(name, coll);
  }

  CollectionInstance &get_or_add_instance(const std::string &name) {
    return _instances.get_or_add(name);
  }

  bool has_instance(const std::string &name) const {
    return _instances.count(name);
  }

  bool del_instance(const std::string &name) {
    return _instances.erase(name);
  }

 private:
  ordered_dict<CollectionInstance> _instances;
};



// Generic primspec container.
// Unknown or unsupported Prim type are also reprenseted as Model for now.
struct Model : public Collection, MaterialBinding {
  std::string name;

  std::string prim_type_name;  // e.g. "" for `def "bora" {}`, "UnknownPrim" for
                               // `def UnknownPrim "bora" {}`
  Specifier spec{Specifier::Def};

  int64_t parent_id{-1};  // Index to parent node

  PrimMeta meta;

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;

  // std::map<std::string, VariantSet> variantSets;

  std::map<std::string, Property> props;

  const std::vector<value::token> &primChildrenNames() const {
    return _primChildren;
  }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

#if 0  // TODO: Remove
// Generic "class" Node
// Mostly identical to GPrim
struct Klass {
  std::string name;
  int64_t parent_id{-1};  // Index to parent node

  std::vector<std::pair<ListEditQual, Reference>> references;

  std::map<std::string, Property> props;
};
#endif

//
// Predefined node classes
//


// Simple volume class.
// Currently this is just an placeholder. Not implemented.

struct OpenVDBAsset {
  std::string fieldDataType{"float"};
  std::string fieldName{"density"};
  std::string filePath;  // asset
};

// MagicaVoxel Vox
struct VoxAsset {
  std::string fieldDataType{"float"};
  std::string fieldName{"density"};
  std::string filePath;  // asset
};

struct Volume {
  OpenVDBAsset vdb;
  VoxAsset vox;
};

// `Scope` is uncommon in graphics community, its something like `Group`.
// From USD doc: Scope is the simplest grouping primitive, and does not carry
// the baggage of transformability.
struct Scope : Collection, MaterialBinding {
  std::string name;
  Specifier spec{Specifier::Def};

  int64_t parent_id{-1};

  PrimMeta meta;

  TypedAttributeWithFallback<Animatable<Visibility>> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, VariantSet> variantSet;

  std::map<std::string, Property> props;

  const std::vector<value::token> &primChildrenNames() const {
    return _primChildren;
  }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

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

#if 0
  ///
  /// Add Prim as a child.
  ///
  ///
  /// @return true Upon success. false when failed(e.g. Prim with same Prim::element_name() already exists) and fill `err` with error message
  ///
  /// Note: This function is thread-safe.
  ///
  bool add_child(Prim &&prim, const std::string &basename, std::string *err = nullptr);
#endif

  //{
  //
  //  _children.emplace_back(std::move(prim));
  //  _child_dirty = true;
  //}

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

#if 0
  // Compute or update world matrix of this Prim.
  // Will traverse child Prims.
  void update_world_matrix(const value::matrix4d &parent_mat);

  const value::matrix4d &get_local_matrix() const;
  const value::matrix4d &get_world_matrix() const;
#endif

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

#if defined(TINYUSDZ_ENABLE_THREAD)
  mutable std::mutex _mutex;
#endif
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
// PrimSpec class is now defined in primspec.hh
#define TINYUSDZ_INSIDE_PRIM_TYPES
#include "primspec.hh"
#undef TINYUSDZ_INSIDE_PRIM_TYPES

struct SubLayer
{
  value::AssetPath assetPath;
  LayerOffset layerOffset;
};


struct LayerMetas {
  enum class PlaybackMode {
    PlaybackModeNone,
    PlaybackModeLoop,
  };

  // TODO: Support more predefined properties: reference =
  // <pxrUSD>/pxr/usd/sdf/wrapLayer.cpp Scene global setting
  TypedAttributeWithFallback<Axis> upAxis{
      Axis::
          Y};  // This can be changed by plugInfo.json in USD:
               // https://graphics.pixar.com/usd/dev/api/group___usd_geom_up_axis__group.html#gaf16b05f297f696c58a086dacc1e288b5
  value::token defaultPrim;                               // prim node name
  TypedAttributeWithFallback<double> metersPerUnit{1.0};  // default [m]
  TypedAttributeWithFallback<double> timeCodesPerSecond{
      24.0};  // default 24 fps
  TypedAttributeWithFallback<double> framesPerSecond{24.0};
  TypedAttributeWithFallback<double> startTimeCode{
      0.0};  // FIXME: default = -inf?
  TypedAttributeWithFallback<double> endTimeCode{
      std::numeric_limits<double>::infinity()};
  std::vector<SubLayer> subLayers;  // `subLayers`
  value::StringData comment;  // 'comment' In Stage meta, comment must be string
                              // only(`comment = "..."` is not allowed)
  value::StringData doc;      // `documentation`

  // UsdPhysics
  TypedAttributeWithFallback<double> kilogramsPerUnit{1.0};

  CustomDataType customLayerData;  // customLayerData

  // USDZ extension
  TypedAttributeWithFallback<bool> autoPlay{
      true};  // default(or not authored) = auto play
  TypedAttributeWithFallback<PlaybackMode> playbackMode{
      PlaybackMode::PlaybackModeLoop};

  // Indirectly used.
  std::vector<value::token> primChildren;
};


// Forward declaration for Layer class
// Layer class has been moved to layer.hh
class Layer;


nonstd::optional<Interpolation> InterpolationFromString(const std::string &v);
nonstd::optional<Orientation> OrientationFromString(const std::string &v);
nonstd::optional<Kind> KindFromString(const std::string &v);

namespace value {


}  // namespace value

namespace prim {

using PropertyMap = std::map<std::string, Property>;
using ReferenceList = std::pair<ListEditQual, std::vector<Reference>>;
using PayloadList = std::pair<ListEditQual, std::vector<Payload>>;

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
