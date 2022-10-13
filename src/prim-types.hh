// SPDX-License-Identifier: MIT
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

#include "primvar.hh"
#include "tiny-variant.hh"

namespace tinyusdz {

// SpecType enum must be same order with pxrUSD's SdfSpecType(since enum value
// is stored in Crate directly)
enum class SpecType {
  Unknown = 0,  // must be 0
  Attribute,
  Connection,
  Expression,
  Mapper,
  MapperArg,
  Prim,
  PseudoRoot,
  Relationship,
  RelationshipTarget,
  Variant,
  VariantSet,
  Invalid,  // or NumSpecTypes
};

enum class Orientation {
  RightHanded,  // 0
  LeftHanded,
  Invalid
};

enum class Visibility {
  Inherited,  // "inherited" (default)
  Invisible,  // "invisible"
  Invalid
};

enum class Purpose {
  Default,  // 0
  Render,   // "render"
  Proxy,    // "proxy"
  Guide,    // "guide"
};

//
// USDZ extension: sceneLibrary
// https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/scenelibrary
//
enum class Kind {
  Model,
  Group,
  Assembly,
  Component,
  Subcomponent,
  SceneLibrary,
  Invalid
};

// Attribute interpolation
enum class Interpolation {
  Constant,     // "constant"
  Uniform,      // "uniform"
  Varying,      // "varying"
  Vertex,       // "vertex"
  FaceVarying,  // "faceVarying"
  Invalid
};

// NOTE: Attribute cannot have ListEdit qualifier
enum class ListEditQual {
  ResetToExplicit,  // "unqualified"(no qualifier)
  Append,           // "append"
  Add,              // "add"
  Delete,           // "delete"
  Prepend,          // "prepend"
  Order,            // "order"
  Invalid
};

enum class Axis { X, Y, Z, Invalid };

// For PrimSpec
enum class Specifier {
  Def,  // 0
  Over,
  Class,
  Invalid
};

enum class Permission {
  Public,  // 0
  Private,
  Invalid
};

enum class Variability {
  Varying,  // 0
  Uniform,
  Config,
  Invalid
};

// single or triple-quoted('"""' or ''') string
struct StringData {
  std::string value;
  bool is_triple_quoted{false};
  bool single_quote{false};  // true for ', false for "

  // optional(for USDA)
  int line_row{0};
  int line_col{0};
};

///
/// Simlar to SdfPath.
/// NOTE: We are doging refactoring of Path class, so the following comment may not be correct.
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
/// - Variant chars('{' '}') is not supported(yet0.
/// - '../' is TODO
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

  Path() : valid(false) {}

  static Path make_root_path() {
    Path p =  Path("/", "");
    // elementPath is empty for root.
    p.element_ = "";
    p.valid = true;
    return p;
  }

  // `p` is split into prim_part and prop_part
  // Path(const std::string &p);
  Path(const std::string &prim, const std::string &prop);

  // : prim_part(prim), valid(true) {}
  // Path(const std::string &prim, const std::string &prop)
  //    : prim_part(prim), prop_part(prop) {}

  Path(const Path &rhs) = default;

  Path &operator=(const Path &rhs) {
    this->valid = rhs.valid;

    this->prim_part = rhs.prim_part;
    this->prop_part = rhs.prop_part;
    this->element_ = rhs.element_;

    return (*this);
  }

  std::string full_path_name() const {
    std::string s;
    if (!valid) {
      s += "#INVALID#";
    }

    s += prim_part;
    if (prop_part.empty()) {
      return s;
    }

    s += "." + prop_part;

    return s;
  }

  std::string GetPrimPart() const { return prim_part; }
  std::string GetPropPart() const { return prop_part; }

  void set_path_type(const PathType ty) { path_type_ = ty; }

  nonstd::optional<PathType> get_path_type() const { return path_type_; }

  // IsPropertyPath: PrimProperty or RelationalAttribute
  bool IsPropertyPath() const {
    if (path_type_) {
      if ((path_type_.value() == PathType::PrimProperty ||
           (path_type_.value() == PathType::RelationalAttribute))) {
        return true;
      }
    }

    // TODO: RelationalAttribute
    if (prim_part.empty()) {
      return false;
    }

    if (prop_part.size()) {
      return true;
    }

    return false;
  }

  // Is Prim's property path?
  // True when both PrimPart and PropPart are not empty.
  bool IsPrimPropertyPath() const {
    if (prim_part.empty()) {
      return false;
    }
    if (prop_part.size()) {
      return true;
    }
    return false;
  }

  bool IsValid() const { return valid; }

  bool IsEmpty() { return (prim_part.empty() && prop_part.empty()); }

  // static Path RelativePath() { return Path("."); }

  Path AppendProperty(const std::string &elem);

  Path AppendElement(const std::string &elem);

  std::string element_name() const { return element_; }

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
  std::pair<Path, Path> SplitAtRoot() const;

  Path GetParentPrim() const;

  ///
  /// @returns true if a path is '/' only
  ///
  bool IsRootPath() const {
    if (!valid) {
      return false;
    }

    if ((prim_part.size() == 1) && (prim_part[0] == '/')) {
      return true;
    }

    return false;
  }

  ///
  /// @returns true if a path is root prim: e.g. '/bora'
  ///
  bool IsRootPrim() const {
    if (!valid) {
      return false;
    }

    if (IsRootPath()) {
      return false;
    }

    if ((prim_part.size() > 1) && (prim_part[0] == '/')) {
      // no other '/' except for the fist one
      if (prim_part.find_last_of('/') == 0) {
        return true;
      }
    }

    return false;
  }

  bool IsAbsolutePath() const {
    if (prim_part.size()) {
      if ((prim_part.size() > 0) && (prim_part[0] == '/')) {
        return true;
      }
    }

    return false;
  }

  bool IsRelativePath() const {
    if (prim_part.size()) {
      return !IsAbsolutePath();
    }

    return true;  // prop part only
  }

  // Strip '/'
  Path &MakeRelative() {
    if (IsAbsolutePath() && (prim_part.size() > 1)) {
      // Remove first '/'
      prim_part.erase(0, 1);
    }
    return *this;
  }

  const Path MakeRelative(Path &&rhs) {
    (*this) = std::move(rhs);

    return MakeRelative();
  }

  static const Path MakeRelative(const Path &rhs) {
    Path p = rhs;  // copy
    return p.MakeRelative();
  }

 private:
  std::string prim_part;  // e.g. /Model/MyMesh, MySphere
  std::string prop_part;  // e.g. .visibility
  std::string element_;   // Element name

  nonstd::optional<PathType> path_type_;  // Currently optional.

  bool valid{false};
};

///
/// Split Path by the delimiter(e.g. "/") then create lists.
///
class TokenizedPath {
 public:
  TokenizedPath() {}

  TokenizedPath(const Path &path) {
    std::string s = path.GetPropPart();
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

class MetaVariable;

using CustomDataType = std::map<std::string, MetaVariable>;

// Variable class for Prim and Attribute Metadataum.
// TODO: Use unify with PrimVar?
class MetaVariable {
 public:
  std::string type;  // Explicit (declared) name of type
  std::string name;
  bool custom{false};

  MetaVariable &operator=(const MetaVariable &rhs) {
    type = rhs.type;
    name = rhs.name;
    custom = rhs.custom;
    value = rhs.value;

    return *this;
  }

  MetaVariable(const MetaVariable &rhs) {
    type = rhs.type;
    name = rhs.name;
    custom = rhs.custom;
    value = rhs.value;
  }

  // template <typename T>
  // bool is() const {
  //   return value.index() == ValueType::index_of<T>();
  // }

  bool Valid() const {
    return value.type_id() != value::TypeTraits<std::nullptr_t>::type_id;
  }

  bool IsObject() const;

  // TODO
  bool IsTimeSamples() const { return false; }

  MetaVariable() = default;

  template <typename T>
  void Set(const T &v) {
    value = v;
  }

  template <typename T>
  nonstd::optional<T> Get() const {
    return value.get_value<T>();
  }

  const value::Value &get_raw() const { return value; }

  const std::string TypeName() const { return type_name(*this); }

  uint32_t TypeId() const { return type_id(*this); }

  bool IsBlocked() const { return (TypeId() == value::TYPE_ID_VALUEBLOCK); }

 private:
  static std::string type_name(const MetaVariable &v) {
    if (!v.type.empty()) {
      return v.type;
    }

    // infer type from value content
    if (v.IsObject()) {
      return "dictionary";
    } else if (v.IsTimeSamples()) {
      std::string ts_type = "TODO: TimeSample type";
      // FIXME
#if 0
      auto ts_struct = v.as_timesamples();

      for (const TimeSampleType &item : ts_struct->values) {
        auto tname = value::type_name(item);
        if (tname != "none") {
          return tname;
        }
      }
#endif

      // ??? TimeSamples data contains all `None` values
      return ts_type;

    } else {
      return v.value.type_name();
    }
  }

  static uint32_t type_id(const MetaVariable &v) {
    // infer type from value content
    if (v.IsObject()) {
      return value::TypeId::TYPE_ID_DICT;
    } else if (v.IsTimeSamples()) {
      return value::TypeId::TYPE_ID_TIMESAMPLES;
    } else {
      return v.value.type_id();
    }
  }

  value::Value value{nullptr};
};

// TimeSample interpolation type.
//
// Held = something like numpy.digitize(right=False)
// https://numpy.org/doc/stable/reference/generated/numpy.digitize.html
//
// Returns `values[i-1]` for `times[i-1] <= t < times[i]`
//
// Linear = linear interpolation
//
// example:
// { 0 : 0.0
//   10 : 1.0
// }
//
// - Held
//   - time 5 = returns 0.0
//   - time 9.99 = returns 0.0
//   - time 10 = returns 1.0
// - Linear
//   - time 5 = returns 0.5
//   - time 9.99 = nearly 1.0
//   - time 10 = 1.0
//
enum class TimeSampleInterpolationType {
  Held,  // something like nearest-neighbor.
  Linear,
};

//
// Supported type for `Linear`
//
// half, float, double, TimeCode(double)
// matrix2d, matrix3d, matrix4d,
// float2h, float3h, float4h
// float2f, float3f, float4f
// float2d, float3d, float4d
// quath, quatf, quatd
// (use slerp for quaternion type)

struct APISchemas {
  // TinyUSDZ does not allow user-supplied API schema for now
  enum class APIName {
    MaterialBindingAPI,  // "MaterialBindingAPI"
    SkelBindingAPI,      // "SkelBindingAPI"
    // USDZ AR extensions
    Preliminary_AnchoringAPI,
    Preliminary_PhysicsColliderAPI,
    // Preliminary_Trigger,
    // Preliminary_PhysicsGravitationalForce,
    Preliminary_PhysicsMaterialAPI,
    Preliminary_PhysicsRigidBodyAPI,
    // Preliminary_InfiniteColliderPlane,
    // Preliminary_ReferenceImage,
    // Preliminary_Action,
    // Preliminary_Text,
  };

  ListEditQual listOpQual{ListEditQual::ResetToExplicit};  // must be 'prepend'

  // std::get<1>: instance name. For Multi-apply API Schema e.g.
  // `material:MainMaterial` for `CollectionAPI:material:MainMaterial`
  std::vector<std::pair<APIName, std::string>> names;
};

// SdfLayerOffset
struct LayerOffset {
  double _offset;
  double _scale;
};

// SdfReference
struct Reference {
  value::AssetPath asset_path;
  Path prim_path;
  LayerOffset layerOffset;
  CustomDataType customData;
};

// SdfPayload
struct Payload {
  value::AssetPath asset_path;  // std::string in SdfPayload
  Path _prim_path;
  LayerOffset _layer_offset;  // from 0.8.0
  // No customData for Payload
};

// Metadata for Prim
struct PrimMeta {
  nonstd::optional<bool> active;                // 'active'
  nonstd::optional<bool> hidden;                // 'hidden'
  nonstd::optional<Kind> kind;                  // 'kind'
  nonstd::optional<CustomDataType> assetInfo;   // 'assetInfo'
  nonstd::optional<CustomDataType> customData;  // `customData`
  nonstd::optional<StringData> doc;             // 'documentation'
  nonstd::optional<StringData> comment;         // 'comment'
  nonstd::optional<APISchemas> apiSchemas;      // 'apiSchemas'

  //
  // Compositions
  //
  nonstd::optional<std::pair<ListEditQual, std::vector<Reference>>> references;
  nonstd::optional<std::pair<ListEditQual, std::vector<Payload>>> payload;
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

  std::map<std::string, MetaVariable> meta;  // other meta values

  // String only metadataum.
  // TODO: Represent as `MetaVariable`?
  std::vector<StringData> stringData;

  // FIXME: Find a better way to detect Prim meta is authored...
  bool authored() const {
    return (active || hidden || kind || customData || references || payload ||
            inherits || variants || variantSets || specializes || sceneName ||
            doc || comment || meta.size() || apiSchemas || stringData.size() ||
            assetInfo);
  }

  //
  // Crate only. Only used internally&debugging.
  //

  nonstd::optional<std::pair<ListEditQual, std::vector<Path>>> inheritPaths;
  nonstd::optional<std::vector<value::token>> primChildren;
  nonstd::optional<std::vector<value::token>> variantChildren;
  nonstd::optional<std::vector<value::token>> variantSetChildren;
};

// Metadata for Attribute
struct AttrMeta {
  // frequently used items
  // nullopt = not specified in USD data
  nonstd::optional<Interpolation> interpolation;  // 'interpolation'
  nonstd::optional<uint32_t> elementSize;         // usdSkel 'elementSize'
  nonstd::optional<bool> hidden;                  // 'hidden'
  nonstd::optional<StringData> comment;           // `comment`
  nonstd::optional<CustomDataType> customData;    // `customData`

  std::map<std::string, MetaVariable> meta;  // other meta values

  // String only metadataum.
  // TODO: Represent as `MetaVariable`?
  std::vector<StringData> stringData;

  bool authored() const {
    return (interpolation || elementSize || hidden || customData ||
            meta.size() || stringData.size());
  }
};

template <typename T>
inline T lerp(const T &a, const T &b, const double t) {
  return (1.0 - t) * a + t * b;
}

template <typename T>
inline std::vector<T> lerp(const std::vector<T> &a, const std::vector<T> &b,
                           const double t) {
  std::vector<T> dst;

  // Choose shorter one
  size_t n = std::min(a.size(), b.size());
  if (n == 0) {
    return dst;
  }

  dst.resize(n);

  if (a.size() != b.size()) {
    return dst;
  }
  for (size_t i = 0; i < n; i++) {
    dst[i] = lerp(a[i], b[i], t);
  }

  return dst;
}

// specializations of lerp
template <>
inline value::AssetPath lerp(const value::AssetPath &a,
                             const value::AssetPath &b, const double t) {
  (void)b;
  (void)t;
  // no interpolation
  return a;
}

template <>
inline std::vector<value::AssetPath> lerp(
    const std::vector<value::AssetPath> &a,
    const std::vector<value::AssetPath> &b, const double t) {
  (void)b;
  (void)t;
  // no interpolation
  return a;
}

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

template <typename T>
struct TypedTimeSamples {
 public:
  struct Sample {
    double t;
    T value;
    bool blocked{false};
  };

  bool empty() const { return _samples.empty(); }

  void Update() {
    std::sort(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; });

    _dirty = false;

    return;
  }

  // Get value at specified time.
  // Return linearly interpolated value when TimeSampleInterpolationType is
  // Linear. Returns nullopt when specified time is out-of-range.
  nonstd::optional<T> TryGet(
      double t = value::TimeCode::Default(),
      TimeSampleInterpolationType interp = TimeSampleInterpolationType::Held) {
    if (empty()) {
      return nonstd::nullopt;
    }

    if (_dirty) {
      Update();
    }

    if (value::TimeCode(t).IsDefault()) {
      // FIXME: Use the first item for now.
      // TODO: Handle bloked
      return _samples[0].value;
    } else {
      auto it = std::lower_bound(
          _samples.begin(), _samples.end(), t,
          [](const Sample &a, double tval) { return a.t < tval; });

      // TODO: Support other interpolation method for example cubic?
      if (interp == TimeSampleInterpolationType::Linear) {
        size_t idx0 = size_t(std::max(
            int64_t(0),
            std::min(int64_t(_samples.size() - 1),
                     int64_t(std::distance(_samples.begin(), it - 1)))));
        size_t idx1 =
            size_t(std::max(int64_t(0), std::min(int64_t(_samples.size() - 1),
                                                 int64_t(idx0) + 1)));

        double tl = _samples[idx0].t;
        double tu = _samples[idx1].t;

        double dt = (t - tl);
        if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
          // slope is zero.
          dt = 0.0;
        } else {
          dt /= (tu - tl);
        }

        // Just in case.
        dt = std::max(0.0, std::min(1.0, dt));

        const T &p0 = _samples[idx0].value;
        const T &p1 = _samples[idx1].value;

        const T p = lerp(p0, p1, dt);

        return std::move(p);
      } else {
        if (it == _samples.end()) {
          // ???
          return nonstd::nullopt;
        }
        return it->value;
      }
    }

    return nonstd::nullopt;
  }

  void AddSample(const Sample &s) {
    _samples.push_back(s);
    _dirty = true;
  }

  void AddSample(const double t, T &v) {
    Sample s;
    s.t = t;
    s.value = v;
    _samples.emplace_back(s);
    _dirty = true;
  }

  void AddBlockedSample(const double t) {
    Sample s;
    s.t = t;
    s.blocked = true;
    _samples.emplace_back(s);
    _dirty = true;
  }

  const std::vector<Sample> &GetSamples() const { return _samples; }

 private:
  // Need to be sorted when look up the value.
  std::vector<Sample> _samples;
  bool _dirty{false};
};

template <typename T>
struct Animatable {
  // scalar
  T value;
  bool blocked{false};

  // timesamples
  TypedTimeSamples<T> ts;

  bool IsTimeSamples() const { return !ts.empty(); }

  bool IsScalar() const { return ts.empty(); }

  // Scalar
  bool IsBlocked() const { return blocked; }

#if 0  // TODO
  T Get() const { return value; }

  T Get(double t) {
    if (IsTimeSampled()) {
      // TODO: lookup value by t
      return timeSamples.Get(t);
    }
    return value;
  }
#endif

  Animatable() {}
  Animatable(const T &v) : value(v) {}
};

///
/// Tyeped Attribute without fallback(default) value.
/// For attribute with `uniform` qualifier or TimeSamples, or have
/// `.connect`(Connection)
///
/// - `authored() = true` : Attribute value is authored(attribute is
/// described in USDA/USDC)
/// - `authored() = false` : Attribute value is not authored(not described
/// in USD). If you call `get()`, fallback value is returned.
///
template <typename T>
class TypedAttribute {
 public:
  void SetValue(const T &v) { _attrib = v; }

  const nonstd::optional<T> GetValue() const {
    if (_attrib) {
      return _attrib.value();
    }
    return nonstd::nullopt;
  }

  // TODO: Animation data.
  bool IsBlocked() const { return _blocked; }

  // for `uniform` attribute only
  void SetBlock(bool onoff) { _blocked = onoff; }

  bool IsConnection() const { return _paths.size(); }

  void SetConnection(const Path &path) {
    _paths.clear();
    _paths.push_back(path);
  }

  void SetConnections(const std::vector<Path> &paths) { _paths = paths; }

  const std::vector<Path> &GetConnections() const { return _paths; }

  const nonstd::optional<Path> GetConnection() const {
    if (_paths.size()) {
      return _paths[0];
    }

    return nonstd::nullopt;
  }

  void SetValueEmpty() { _empty = true; }

  bool IsValueEmpty() const { return _empty; }

  // value set?
  bool authored() const {
    if (_empty) {
      return true;
    }

    if (_attrib) {
      return true;
    }
    if (_paths.size()) {
      return true;
    }
    return false;
  }

  AttrMeta meta;

 private:
  bool _empty{false};
  std::vector<Path> _paths;
  nonstd::optional<T> _attrib;
  bool _blocked{false};  // for `uniform` attribute.
};

///
/// Tyeped Terminal(Output) Attribute(No value assign, no fallback(default)
/// value, no connection)
///
/// - `authored() = true` : Attribute value is authored(attribute is
/// described in USDA/USDC)
/// - `authored() = false` : Attribute value is not authored(not described
/// in USD).
///
template <typename T>
class TypedTerminalAttribute {
 public:
  void SetAuthor(bool onoff) { _authored = onoff; }

  // value set?
  bool authored() const { return _authored; }

  std::string type_name() const { return value::TypeTraits<T>::type_name(); }

  uint32_t type_id() const { return value::TypeTraits<T>::type_id; }

  AttrMeta meta;

 private:
  bool _authored{false};
};

template <typename T>
class TypedAttributeWithFallback;

///
/// Attribute with fallback(default) value.
/// For attribute with `uniform` qualifier or TimeSamples, but don't have
/// `.connect`(Connection)
///
/// - `authored() = true` : Attribute value is authored(attribute is
/// described in USDA/USDC)
/// - `authored() = false` : Attribute value is not authored(not described
/// in USD). If you call `get()`, fallback value is returned.
///
template <typename T>
class TypedAttributeWithFallback {
 public:
  TypedAttributeWithFallback() = delete;

  ///
  /// Init with fallback value;
  ///
  TypedAttributeWithFallback(const T &fallback) : _fallback(fallback) {}

  TypedAttributeWithFallback &operator=(const T &value) {
    _attrib = value;

    // fallback Value should be already set with `AttribWithFallback(const T&
    // fallback)` constructor.

    return (*this);
  }

  //
  // FIXME: Defininig copy constructor, move constructor and  move assignment
  // operator Gives compilation error :-(. so do not define it.
  //

  // AttribWithFallback(const AttribWithFallback &rhs) {
  //   attrib = rhs.attrib;
  //   fallback = rhs.fallback;
  // }

  // AttribWithFallback &operator=(T&& value) noexcept {
  //   if (this != &value) {
  //       attrib = std::move(value.attrib);
  //       fallback = std::move(value.fallback);
  //   }
  //   return (*this);
  // }

  // AttribWithFallback(AttribWithFallback &&rhs) noexcept {
  //   if (this != &rhs) {
  //       attrib = std::move(rhs.attrib);
  //       fallback = std::move(rhs.fallback);
  //   }
  // }

  void SetValue(const T &v) { _attrib = v; }

  void SetValueEmpty() { _empty = true; }

  bool IsValueEmpty() const { return _empty; }

  // TODO: Animation data.
  const T &GetValue() const {
    if (_attrib) {
      return _attrib.value();
    }
    return _fallback;
  }

  // TODO: Animation data.
  bool IsBlocked() const { return _blocked; }

  // for `uniform` attribute only
  void SetBlock(bool onoff) { _blocked = onoff; }

  bool IsConnection() const { return _paths.size(); }

  void SetConnection(const Path &path) {
    _paths.clear();
    _paths.push_back(path);
  }

  void SetConnections(const std::vector<Path> &paths) { _paths = paths; }

  const std::vector<Path> &GetConnections() const { return _paths; }

  const nonstd::optional<Path> GetConnection() const {
    if (_paths.size()) {
      return _paths[0];
    }

    return nonstd::nullopt;
  }

  // value set?
  bool authored() const {
    if (_empty) {  // authored with empty value.
      return true;
    }
    if (_attrib) {
      return true;
    }
    if (_paths.size()) {
      return true;
    }
    if (_blocked) {
      return true;
    }
    return false;
  }

  AttrMeta meta;

 private:
  std::vector<Path> _paths;
  nonstd::optional<T> _attrib;
  bool _empty{false};
  T _fallback;
  bool _blocked{false};  // for `uniform` attribute.
};

template <typename T>
using TypedAnimatableAttributeWithFallback =
    TypedAttributeWithFallback<Animatable<T>>;

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

template <typename T>
class ListOp {
 public:
  ListOp() : is_explicit(false) {}

  void ClearAndMakeExplicit() {
    explicit_items.clear();
    added_items.clear();
    prepended_items.clear();
    appended_items.clear();
    deleted_items.clear();
    ordered_items.clear();

    is_explicit = true;
  }

  bool IsExplicit() const { return is_explicit; }
  bool HasExplicitItems() const { return explicit_items.size(); }

  bool HasAddedItems() const { return added_items.size(); }

  bool HasPrependedItems() const { return prepended_items.size(); }

  bool HasAppendedItems() const { return appended_items.size(); }

  bool HasDeletedItems() const { return deleted_items.size(); }

  bool HasOrderedItems() const { return ordered_items.size(); }

  const std::vector<T> &GetExplicitItems() const { return explicit_items; }

  const std::vector<T> &GetAddedItems() const { return added_items; }

  const std::vector<T> &GetPrependedItems() const { return prepended_items; }

  const std::vector<T> &GetAppendedItems() const { return appended_items; }

  const std::vector<T> &GetDeletedItems() const { return deleted_items; }

  const std::vector<T> &GetOrderedItems() const { return ordered_items; }

  void SetExplicitItems(const std::vector<T> &v) { explicit_items = v; }

  void SetAddedItems(const std::vector<T> &v) { added_items = v; }

  void SetPrependedItems(const std::vector<T> &v) { prepended_items = v; }

  void SetAppendedItems(const std::vector<T> &v) { appended_items = v; }

  void SetDeletedItems(const std::vector<T> &v) { deleted_items = v; }

  void SetOrderedItems(const std::vector<T> &v) { ordered_items = v; }

 private:
  bool is_explicit{false};
  std::vector<T> explicit_items;
  std::vector<T> added_items;
  std::vector<T> prepended_items;
  std::vector<T> appended_items;
  std::vector<T> deleted_items;
  std::vector<T> ordered_items;
};

struct ListOpHeader {
  enum Bits {
    IsExplicitBit = 1 << 0,
    HasExplicitItemsBit = 1 << 1,
    HasAddedItemsBit = 1 << 2,
    HasDeletedItemsBit = 1 << 3,
    HasOrderedItemsBit = 1 << 4,
    HasPrependedItemsBit = 1 << 5,
    HasAppendedItemsBit = 1 << 6
  };

  ListOpHeader() : bits(0) {}

  explicit ListOpHeader(uint8_t b) : bits(b) {}

  explicit ListOpHeader(ListOpHeader const &op) : bits(0) {
    bits |= op.IsExplicit() ? IsExplicitBit : 0;
    bits |= op.HasExplicitItems() ? HasExplicitItemsBit : 0;
    bits |= op.HasAddedItems() ? HasAddedItemsBit : 0;
    bits |= op.HasPrependedItems() ? HasPrependedItemsBit : 0;
    bits |= op.HasAppendedItems() ? HasAppendedItemsBit : 0;
    bits |= op.HasDeletedItems() ? HasDeletedItemsBit : 0;
    bits |= op.HasOrderedItems() ? HasOrderedItemsBit : 0;
  }

  bool IsExplicit() const { return bits & IsExplicitBit; }

  bool HasExplicitItems() const { return bits & HasExplicitItemsBit; }
  bool HasAddedItems() const { return bits & HasAddedItemsBit; }
  bool HasPrependedItems() const { return bits & HasPrependedItemsBit; }
  bool HasAppendedItems() const { return bits & HasAppendedItemsBit; }
  bool HasDeletedItems() const { return bits & HasDeletedItemsBit; }
  bool HasOrderedItems() const { return bits & HasOrderedItemsBit; }

  uint8_t bits;
};

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

// ret = m x n
template <typename MTy, typename STy, size_t N>
MTy Mult(const MTy &m, const MTy &n) {
  MTy ret;
  memset(ret.m, 0, sizeof(MTy));

  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; i < N; i++) {
      STy value = static_cast<STy>(0);
      for (size_t k = 0; k < N; k++) {
        value += m.m[k][i] * n.m[j][k];
      }
      ret.m[j][i] = value;
    }
  }

  return ret;
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

  bool Valid() const {
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

// Relation
class Relation {
 public:
  // monostate(empty(define only)), string, Path or PathVector
  // tinyusdz::variant<tinyusdz::monostate, std::string, Path,
  // std::vector<Path>> targets;

  // For some reaon, using tinyusdz::variant will cause double-free in some
  // environemt on clang, so use old-fashioned way for a while.
  enum class Type { Empty, String, Path, PathVector };

  Type type{Type::Empty};
  std::string targetString;
  Path targetPath;
  std::vector<Path> targetPathVector;
  ListEditQual listOpQual{ListEditQual::ResetToExplicit};

  static Relation MakeEmpty() {
    Relation r;
    r.SetEmpty();
    return r;
  }

  void SetListEditQualifier(ListEditQual q) { listOpQual = q; }

  ListEditQual GetListEditQualifier() const { return listOpQual; }

  void SetEmpty() { type = Type::Empty; }

  void Set(const std::string &s) {
    targetString = s;
    type = Type::String;
  }

  void Set(const Path &p) {
    targetPath = p;
    type = Type::Path;
  }

  void Set(const std::vector<Path> &pv) {
    targetPathVector = pv;
    type = Type::PathVector;
  }

  bool IsEmpty() const { return type == Type::Empty; }

  bool IsString() const { return type == Type::String; }

  bool IsPath() const { return type == Type::Path; }

  bool IsPathVector() const { return type == Type::PathVector; }

  AttrMeta meta;
};

//
// Connection is a typed version of Relation
//
template <typename T>
class Connection {
 public:
  using type = typename value::TypeTraits<T>::value_type;

  static std::string type_name() { return value::TypeTraits<T>::type_name(); }

  // Connection() = delete;
  // Connection(const T &v) : fallback(v) {}

  nonstd::optional<Path> target;
};

// PrimAttrib is a struct to hold generic attribute of a property(e.g. primvar)
struct PrimAttrib {
  std::string name;  // attrib name

  void set_type_name(const std::string &tname) { _type_name = tname; }

  // `var` may be empty, so store type info with set_type_name and set_type_id.
  std::string type_name() const {
    if (_type_name.size()) {
      return _type_name;
    }

    // Fallback. May be unreliable(`var` could be empty).
    return _var.type_name();
  }

  Variability variability{
      Variability::Varying};  // 'uniform` qualifier is handled with
                              // `variability=uniform`

  // Interpolation interpolation{Interpolation::Invalid};

  AttrMeta meta;

  void set_var(primvar::PrimVar &&v) {
    if (_type_name.empty()) {
      _type_name = v.type_name();
    }

    _var = std::move(v);
  }

  template <typename T>
  nonstd::optional<T> get_value() const {
    return _var.get_value<T>();
  }

  const primvar::PrimVar &get_var() const { return _var; }

  void set_blocked(bool onoff) { _blocked = onoff; }

  bool blocked() const { return _blocked; }

 private:
  bool _blocked{false};  // Attribute Block('None')
  std::string _type_name;
  primvar::PrimVar _var;
};

#if 0
///
/// Typed version of Property(e.g. for `points`, `normals`, `velocities.timeSamples`, `inputs:st.connect`)
/// (but no Relation)
/// TODO: Use TypedAttribute since this class does not store Relation information.
///
template <typename T>
class TypedProperty {
 public:
  TypedProperty() = default;

  explicit TypedProperty(const T &fv) : fallback(fv) {}

  using type = typename value::TypeTraits<T>::value_type;

  static std::string type_name() { return value::TypeTraits<T>::type_name(); }

  // TODO: Use variant?
  nonstd::optional<Animatable<T>> value; // T or TimeSamples<T>
  nonstd::optional<Path> target;

  //bool IsRel() const {
  //  return (value::TypeTraits<T>::type_id == value::TypeTraits<Relation>::type_id)
  //}

  bool IsConnection() const {
    return target.has_value();
  }

  bool IsAttrib() const {
    return value.has_value();
  }

  bool IsEmptyAttrib() const {
    return define_only;
  }

  bool authored() const {
    if (define_only) {
      return true;
    }

    return (target || value);
  }

  bool set_define_only(bool onoff = true) { define_only = onoff; return define_only; }

  nonstd::optional<T> fallback;  // may have fallback
  AttrMeta meta;
  bool custom{false}; // `custom`
  Variability variability{Variability::Varying}; // `uniform`, `varying`

  // TODO: Other variability
  bool define_only{false}; // Attribute must be define-only(no value or connection assigned). e.g. "float3 outputs:rgb"
  //ListEditQual listOpQual{ListEditQual::ResetToExplicit}; // default = "unqualified"
};
#endif

// Generic container for Attribute or Relation/Connection. And has this property
// is custom or not (Need to lookup schema if the property is custom or not for
// Crate data)
class Property {
 public:
  enum class Type {
    EmptyAttrib,        // Attrib with no data.
    Attrib,             // contains actual data
    Relation,           // `rel` type
    NoTargetsRelation,  // `rel` with no targets.
    Connection,         // `.connect` suffix
  };

  Property() = default;

  Property(const std::string &type_name, bool custom) : _has_custom(custom) {
    _attrib.set_type_name(type_name);
    _type = Type::EmptyAttrib;
  }

  Property(const PrimAttrib &a, bool custom) : _attrib(a), _has_custom(custom) {
    _type = Type::Attrib;
  }

  Property(PrimAttrib &&a, bool custom)
      : _attrib(std::move(a)), _has_custom(custom) {
    _type = Type::Attrib;
  }

  // Relation: typeless
  Property(const Relation &r, bool custom) : _rel(r), _has_custom(custom) {
    _type = Type::Relation;
  }

  Property(Relation &&r, bool custom)
      : _rel(std::move(r)), _has_custom(custom) {
    _type = Type::Relation;
  }

  // Attribute Connection: has type
  Property(const Relation &r, const std::string &prop_value_type_name,
           bool custom)
      : _rel(r),
        _prop_value_type_name(prop_value_type_name),
        _has_custom(custom) {
    _type = Type::Connection;
  }

  Property(Relation &&r, const std::string &prop_value_type_name, bool custom)
      : _rel(std::move(r)),
        _prop_value_type_name(prop_value_type_name),
        _has_custom(custom) {
    _type = Type::Connection;
  }

  bool IsAttrib() const {
    return (_type == Type::EmptyAttrib) || (_type == Type::Attrib);
  }
  bool IsEmpty() const {
    return (_type == Type::EmptyAttrib) || (_type == Type::NoTargetsRelation);
  }
  bool IsRel() const {
    return (_type == Type::Relation) || (_type == Type::NoTargetsRelation);
  }
  bool IsConnection() const { return _type == Type::Connection; }

  nonstd::optional<Path> GetConnectionTarget() const {
    if (!IsConnection()) {
      return nonstd::nullopt;
    }

    if (_rel.IsPath()) {
      return _rel.targetPath;
    }

    return nonstd::nullopt;
  }

  std::string value_type_name() const {
    if (IsConnection()) {
      return _prop_value_type_name;
    } else if (IsRel()) {
      // relation is typeless.
      return std::string();
    } else {
      return _attrib.type_name();
    }
  }

  bool HasCustom() const { return _has_custom; }

  void SetPropetryType(Type ty) { _type = ty; }

  Type GetPropertyType() const { return _type; }

  void SetListEditQual(ListEditQual qual) { _listOpQual = qual; }

  const PrimAttrib &GetAttrib() const { return _attrib; }

  PrimAttrib &GetAttrib() { return _attrib; }

  void SetAttrib(const PrimAttrib &attrib) {
    _attrib = attrib;
    _type = Type::Attrib;
  }

  const Relation &GetRelation() const { return _rel; }

  Relation &GetRelation() { return _rel; }

  ListEditQual GetListEditQual() const { return _listOpQual; }

 private:
  PrimAttrib _attrib;

  // List Edit qualifier(Attribute can never be list editable)
  // TODO:  Store listEdit qualifier to `Relation`
  ListEditQual _listOpQual{ListEditQual::ResetToExplicit};

  Type _type{Type::EmptyAttrib};
  Relation _rel;  // Relation(`rel`) or Connection(`.connect`)
  std::string _prop_value_type_name;  // for Connection.
  bool _has_custom{false};            // Qualified with 'custom' keyword?
};

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
  OpType op;
  bool inverted{false};  // true when `!inverted!` prefix
  std::string
      suffix;  // may contain nested namespaces. e.g. suffix will be
               // ":blender:pivot" for "xformOp:translate:blender:pivot". Suffix
               // will be empty for "xformOp:translate"
  // XformOpValueType value_type;
  // std::string type_name;

  value::TimeSamples var;

  std::string get_value_type_name() const {
    if (var.values.size() > 0) {
      return var.values[0].type_name();
    }
    return "[InternalError] XformOp value type name";
  }

  uint32_t get_value_type_id() const {
    if (var.values.size() > 0) {
      return var.values[0].type_id();
    }
    return uint32_t(value::TypeId::TYPE_ID_INVALID);
  }

  // TODO: Check if T is valid type.
  template <class T>
  void set_scalar(const T &v) {
    var.times.clear();
    var.values.clear();

    var.values.push_back(v);
    // type_name = value::TypeTraits<T>::type_name();
  }

  void set_timesamples(const value::TimeSamples &v) {
    var = v;

    // if (var.values.size()) {
    //   type_name = var.values[0].type_name();
    // }
  }

  void set_timesamples(value::TimeSamples &&v) {
    var = std::move(v);
    // if (var.values.size()) {
    //   type_name = var.values[0].type_name();
    // }
  }

  bool is_timesamples() const {
    return (var.times.size() > 0) && (var.times.size() == var.values.size());
  }

  nonstd::optional<value::TimeSamples> get_timesamples() const {
    if (is_timesamples()) {
      return var;
    }
    return nonstd::nullopt;
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_scalar_value() const {
    if (is_timesamples()) {
      return nonstd::nullopt;
    }

#if 0
    if (value::TypeTraits<T>::type_id == var.values[0].type_id()) {
      //return std::move(*reinterpret_cast<const T *>(var.values[0].value()));
      auto pv = linb::any_cast<const T>(&var.values[0]);
      if (pv) {
        return (*pv);
      }
      return nonstd::nullopt;
    } else if (value::TypeTraits<T>::underlying_type_id == var.values[0].underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      //return *reinterpret_cast<const T *>(var.values[0].value());

      // TODO: strict type check.
      return *linb::cast<const T>(&var.values[0]);
    }
    return nonstd::nullopt;
#else
    return var.values[0].get_value<T>();
#endif
  }
};

// Interpolator for TimeSample data
enum class TimeSampleInterpolation {
  Nearest,  // nearest neighbor
  Linear,   // lerp
  // TODO: more to support...
};

#if 0

template <typename T>
struct TimeSamples {
  std::vector<double> times;
  std::vector<T> values;
  // TODO: Support `none`

  void Set(T value, double t) {
    times.push_back(t);
    values.push_back(value);
  }

  T Get(double t) const {
    // Linear-interpolation.
    // TODO: Support other interpolation method for example cubic.
    auto it = std::lower_bound(times.begin(), times.end(), t);
    size_t idx0 = size_t(std::max(
        int64_t(0), std::min(int64_t(times.size() - 1),
                             int64_t(std::distance(times.begin(), it - 1)))));
    size_t idx1 = size_t(std::max(
        int64_t(0), std::min(int64_t(times.size() - 1), int64_t(idx0) + 1)));

    double tl = times[idx0];
    double tu = times[idx1];

    double dt = (t - tl);
    if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
      // slope is zero.
      dt = 0.0;
    } else {
      dt /= (tu - tl);
    }

    // Just in case.
    dt = std::max(0.0, std::min(1.0, dt));

    const T &p0 = values[idx0];
    const T &p1 = values[idx1];

    const T p = lerp(p0, p1, dt);

    return p;
  }

  bool Valid() const {
    return !times.empty() && (times.size() == values.size());
  }
};
#endif

// Prim metas, Prim tree and properties.
struct VariantSet {
  PrimMeta metas;
  std::vector<int64_t> primIndices;
  std::map<std::string, Property> props;
};

// Generic primspec container.
struct Model {
  std::string name;

  Specifier spec{Specifier::Def};

  int64_t parent_id{-1};  // Index to parent node

  PrimMeta meta;

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;

  std::map<std::string, VariantSet> variantSet;

  std::map<std::string, Property> props;
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

struct MaterialBindingAPI {
  Path binding;            // rel material:binding
  Path bindingCorrection;  // rel material:binding:correction
  Path bindingPreview;     // rel material:binding:preview

  // TODO: allPurpose, preview, ...
};

//
// Predefined node classes
//

// USDZ Schemas for AR
// https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/schema_definitions_for_third-party_digital_content_creation_dcc

// UsdPhysics
struct Preliminary_PhysicsGravitationalForce {
  // physics::gravitatioalForce::acceleration
  value::double3 acceleration{{0.0, -9.81, 0.0}};  // [m/s^2]
};

struct Preliminary_PhysicsMaterialAPI {
  // preliminary:physics:material:restitution
  double restitution;  // [0.0, 1.0]

  // preliminary:physics:material:friction:static
  double friction_static;

  // preliminary:physics:material:friction:dynamic
  double friction_dynamic;
};

struct Preliminary_PhysicsRigidBodyAPI {
  // preliminary:physics:rigidBody:mass
  double mass{1.0};

  // preliminary:physics:rigidBody:initiallyActive
  bool initiallyActive{true};
};

struct Preliminary_PhysicsColliderAPI {
  // preliminary::physics::collider::convexShape
  Path convexShape;
};

struct Preliminary_InfiniteColliderPlane {
  value::double3 position{{0.0, 0.0, 0.0}};
  value::double3 normal{{0.0, 0.0, 0.0}};

  Extent extent;  // [-FLT_MAX, FLT_MAX]

  Preliminary_InfiniteColliderPlane() {
    extent.lower[0] = -(std::numeric_limits<float>::max)();
    extent.lower[1] = -(std::numeric_limits<float>::max)();
    extent.lower[2] = -(std::numeric_limits<float>::max)();
    extent.upper[0] = (std::numeric_limits<float>::max)();
    extent.upper[1] = (std::numeric_limits<float>::max)();
    extent.upper[2] = (std::numeric_limits<float>::max)();
  }
};

// UsdInteractive
struct Preliminary_AnchoringAPI {
  // preliminary:anchoring:type
  std::string type;  // "plane", "image", "face", "none";

  std::string alignment;  // "horizontal", "vertical", "any";

  Path referenceImage;
};

struct Preliminary_ReferenceImage {
  int64_t image_id{-1};  // asset image

  double physicalWidth{0.0};
};

struct Preliminary_Behavior {
  Path triggers;
  Path actions;
  bool exclusive{false};
};

struct Preliminary_Trigger {
  // uniform token info:id
  std::string info;  // Store decoded string from token id
};

struct Preliminary_Action {
  // uniform token info:id
  std::string info;  // Store decoded string from token id

  std::string multiplePerformOperation{
      "ignore"};  // ["ignore", "allow", "stop"]
};

struct Preliminary_Text {
  std::string content;
  std::vector<std::string> font;  // An array of font names

  float pointSize{144.0f};
  float width;
  float height;
  float depth{0.0f};

  std::string wrapMode{"flowing"};  // ["singleLine", "hardBreaks", "flowing"]
  std::string horizontalAlignmment{
      "center"};  // ["left", "center", "right", "justified"]
  std::string verticalAlignmment{
      "middle"};  // ["top", "middle", "lowerMiddle", "baseline", "bottom"]
};

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
struct Scope {
  std::string name;
  Specifier spec{Specifier::Def};

  int64_t parent_id{-1};

  PrimMeta meta;

  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, VariantSet> variantSet;

  std::map<std::string, Property> props;
};

//
// For `Stage` scene graph.
// Similar to `Prim` in pxrUSD.
// This class uses tree-representation of `Prim`. Easy to use, but may not be
// performant than flattened Prim array + index representation of Prim tree(Index-based scene graph such like glTF).
//
class Prim {
 public:

  Prim(const value::Value &rhs);
  Prim(value::Value &&rhs);

  template<typename T>
  Prim(const T &prim) {
    // Check if T is Prim class type.
    static_assert((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id) && (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id), "T is not a Prim class type");
    _data = prim;
  }

  std::vector<Prim> &children() {
    return _children;
  }

  const std::vector<Prim> &children() const {
    return _children;
  }

  const value::Value &data() const {
    return _data;
  }

  Specifier &specifier() {
    return _specifier;
  }

  Specifier specifier() const {
    return _specifier;
  }

  Path &local_path() {
    return _path;
  }

  const Path &local_path() const {
    return _path;
  }


  Path &element_path() {
    return _elementPath;
  }

  const Path &element_path() const {
    return _elementPath;
  }

  template<typename T>
  bool is() const {
    return (_data.type_id() == value::TypeTraits<T>::type_id);
  }

  // Return a pointer of a concrete Prim class(Xform, Material, ...)
  // Return nullptr when failed to cast or T is not a Prim type.
  template<typename T>
  const T* as() const {
    // Check if T is Prim type. e.g. Xform, Material, ...
    if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id) &&
        (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id)) {
      return _data.as<T>();
    }

    return nullptr;
  }

 private:
  Path _path;  // Prim's local path name. May contain Property, Relation and
              // other infos, but do not include parent's path. To get fully absolute path of a Prim(e.g. "/xform0/mymesh0", You need to traverse Prim tree and concatename `elementPath` or use ***(T.B.D>) method in `Stage` class
  Path _elementPath;  // leaf("terminal") Prim name.(e.g. "myxform" for `def
                     // Xform "myform"`). For root node, elementPath name is
                     // empty string("").
  Specifier _specifier{
      Specifier::Invalid};  // `def`, `over` or `class`. Usually `def`
  value::Value _data;  // Generic container for concrete Prim object. GPrim, Xform, ...
  std::vector<Prim> _children;  // child Prim nodes
};

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
    const auto m = vsmap.find(target_name);
    if (m != vsmap.end()) {
      current_vsmap[target_name] = variant_name;
      return true;
    } else {
      return false;
    }
  }

  ///
  /// List variants in this Prim
  /// key = variant prim name
  /// value = variats
  ///
  const VariantSelectionMap &get_variant_selection_map() const { return vsmap; }

  ///
  /// Variants
  ///
  /// variant element = Property or Prim
  ///
  using PropertyMap = std::map<std::string, Property>;
  using PrimNodeMap = std::map<std::string, PrimNode>;

  VariantSelectionMap vsmap;          // Original variant selections
  VariantSelectionMap current_vsmap;  // Currently selected variants

  // key = variant_name
  std::map<std::string, PropertyMap> variantAttributeMap;
  std::map<std::string, PrimNodeMap> variantPrimNodeMap;

  ///
  /// Information for Crate(USDC binary)
  ///
  std::vector<value::token> primChildren;
  std::vector<value::token> variantChildren;
};

#if 0  // TODO: Remove
//
// For low-level scene graph representation, something like Vulkan.
// Less abstraction, and scene graph is representated by indices.
//
struct Node {
  std::string name;

  value::TypeId type_id{value::TypeId::TYPE_ID_INVALID};

  //
  // index to a `Scene::node_indices`
  //
  int64_t index{-1};

  int64_t parent{-1};          // parent node index
  std::vector<Node> children;  // child nodes
};
#endif

#if 0

struct StageMetas {
  // TODO: Support more predefined properties: reference = <pxrUSD>/pxr/usd/sdf/wrapLayer.cpp
  // Scene global setting
  TypedAttributeWithFallback<Axis> upAxis{Axis::Y}; // This can be changed by plugInfo.json in USD: https://graphics.pixar.com/usd/dev/api/group___usd_geom_up_axis__group.html#gaf16b05f297f696c58a086dacc1e288b5
  value::token defaultPrim;           // prim node name
  TypedAttributeWithFallback<double> metersPerUnit{1.0};        // default [m]
  TypedAttributeWithFallback<double> timeCodesPerSecond {24.0};  // default 24 fps
  TypedAttributeWithFallback<double> framesPerSecond {24.0};  // FIXME: default 24 fps
  TypedAttributeWithFallback<double> startTimeCode{0.0}; // FIXME: default = -inf?
  TypedAttributeWithFallback<double> endTimeCode{std::numeric_limits<double>::infinity()};
  std::vector<value::AssetPath> subLayers; // `subLayers`
  StringData comment; // 'comment'
  StringData doc; // `documentation`

  CustomDataType customLayerData; // customLayerData

  // String only metadataum.
  // TODO: Represent as `MetaVariable`?
  std::vector<StringData> stringData;
};

class PrimRange;

// Similar to UsdStage, but much more something like a Scene(scene graph)
class Stage {
 public:

  static Stage CreateInMemory() {
    return Stage();
  }

  ///
  /// Traverse by depth-first order.
  ///
  PrimRange Traverse();

  ///
  /// Get Prim at a Path.
  ///
  /// @returns pointer to Prim(to avoid a copy). Never return nullptr upon success.
  ///
  nonstd::expected<const Prim *, std::string> GetPrimAtPath(const Path &path);

  ///
  /// Dump Stage as ASCII(USDA) representation.
  ///
  std::string ExportToString() const;


  const std::vector<Prim> &GetRootPrims() const {
    return root_nodes;
  }

  std::vector<Prim> &GetRootPrims() {
    return root_nodes;
  }

  const StageMetas &GetMetas() const {
    return stage_metas;
  }

  StageMetas &GetMetas() {
    return stage_metas;
  }

  ///
  /// Compose scene.
  ///
  bool Compose(bool addSourceFileComment = true) const;

  ///
  /// pxrUSD Compat API
  ///
  bool Flatten(bool addSourceFileComment = true) const {
    return Compose(addSourceFileComment);
  }


 private:

  // Root nodes
  std::vector<Prim> root_nodes;

  std::string name;       // Scene name
  int64_t default_root_node{-1};  // index to default root node

  StageMetas stage_metas;

  mutable std::string _err;
  mutable std::string _warn;

  // Cache prim path.
  std::map<Path, const Prim *> _prim_path_cache;
  bool _dirty{false};

};
#endif

// Simple bidirectional Path(string) <-> index lookup
struct StringAndIdMap {
  void add(int32_t key, const std::string &val) {
    _i_to_s[key] = val;
    _s_to_i[val] = key;
  }

  void add(const std::string &key, int32_t val) {
    _s_to_i[key] = val;
    _i_to_s[val] = key;
  }

  size_t count(int32_t i) const { return _i_to_s.count(i); }

  size_t count(const std::string &s) const { return _s_to_i.count(s); }

  std::string at(int32_t i) const { return _i_to_s.at(i); }

  int32_t at(std::string s) const { return _s_to_i.at(s); }

  std::map<int32_t, std::string> _i_to_s;  // index -> string
  std::map<std::string, int32_t> _s_to_i;  // string -> index
};

struct NodeIndex {
  std::string name;

  // TypeTraits<T>::type_id
  value::TypeId type_id{value::TypeId::TYPE_ID_INVALID};

  int64_t index{-1};  // array index to `Scene::xforms`, `Scene::geom_cameras`,
                      // ... -1 = invlid(or not set)
};

nonstd::optional<Interpolation> InterpolationFromString(const std::string &v);
nonstd::optional<Orientation> OrientationFromString(const std::string &v);
nonstd::optional<Kind> KindFromString(const std::string &v);

// Return false when invalid character(e.g. '%') exists.
bool ValidatePrimName(const std::string &tok);

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
DEFINE_TYPE_TRAIT(Relation, "Relationship", TYPE_ID_RELATIONSHIP, 1);
// TODO(syoyo): Define PathVector as 1D array?
DEFINE_TYPE_TRAIT(std::vector<Path>, "PathVector", TYPE_ID_PATH_VECTOR, 1);

DEFINE_TYPE_TRAIT(std::vector<value::token>, "token[]", TYPE_ID_TOKEN_VECTOR,
                  1);

DEFINE_TYPE_TRAIT(value::TimeSamples, "TimeSamples", TYPE_ID_TIMESAMPLES, 1);

DEFINE_TYPE_TRAIT(Model, "Model", TYPE_ID_MODEL, 1);
DEFINE_TYPE_TRAIT(Scope, "Scope", TYPE_ID_SCOPE, 1);

DEFINE_TYPE_TRAIT(StringData, "string", TYPE_ID_STRING_DATA, 1);

DEFINE_TYPE_TRAIT(CustomDataType, "customData", TYPE_ID_CUSTOMDATA,
                  1);  // TODO: Unify with `dict`?

DEFINE_TYPE_TRAIT(Extent, "float3[]", TYPE_ID_EXTENT, 2);  // float3[2]

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

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
