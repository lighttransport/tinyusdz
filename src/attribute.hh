// SPDX-License-Identifier: Apache 2.0

///
/// @file attribute.hh
/// @brief USD Attribute class definition
///
/// Attributes are the primary container for typed data values in USD.
/// They can hold constant values, time-sampled values, or connections
/// to other attributes.
///
#pragma once

#include <string>
#include <vector>

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
#include "value-types.hh"
#include "value-eval-util.hh"
#include "path.hh"
#include "enum-types.hh"
#include "dictionary.hh"
#include "logger.hh"

//#ifndef TINYUSDZ_INSIDE_PRIM_TYPES
namespace tinyusdz {
//#endif

// Forward declarations - these must be defined before including this file
class Path;
struct AttrMetas;
using AttrMeta = AttrMetas;


// Metadata for Property(Relationship and Attribute)
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

///
/// @brief USD Attribute class
///
/// Attributes are typed properties that can hold values, time-sampled data,
/// or connections to other attributes. They form the primary data storage
/// mechanism in USD for geometric data, material parameters, and other
/// scene properties.
///
class Attribute {

 public:
  Attribute() : _metas(nullptr) {
    TUSDZ_LOG_I("Attribute default constructor called");
  }

  ~Attribute() {
    delete _metas;
  }

  // Copy constructor
  Attribute(const Attribute& rhs) 
    : _name(rhs._name),
      _variability(rhs._variability),
      _varying_authored(rhs._varying_authored),
      _type_name(rhs._type_name),
      _var(rhs._var),
      _paths(rhs._paths) {
    TUSDZ_LOG_I("Attribute copy constructor called");
    if (rhs._metas) {
      _metas = new AttrMetas(*rhs._metas);
    }
  }

  // Move constructor
  Attribute(Attribute&& rhs) noexcept
    : _name(std::move(rhs._name)),
      _variability(rhs._variability),
      _varying_authored(rhs._varying_authored),
      _type_name(std::move(rhs._type_name)),
      _var(std::move(rhs._var)),
      _paths(std::move(rhs._paths)),
      _metas(rhs._metas) {
    TUSDZ_LOG_I("Attribute move constructor called");
    rhs._variability = Variability::Varying;
    rhs._varying_authored = false;
    rhs._metas = nullptr;
  }

  // Copy assignment operator
  Attribute& operator=(const Attribute& rhs) {
    TUSDZ_LOG_I("Attribute copy assignment operator called");
    if (this != &rhs) {
      _name = rhs._name;
      _variability = rhs._variability;
      _varying_authored = rhs._varying_authored;
      _type_name = rhs._type_name;
      _var = rhs._var;
      _paths = rhs._paths;
      delete _metas;
      _metas = rhs._metas ? new AttrMetas(*rhs._metas) : nullptr;
    }
    return *this;
  }

  // Move assignment operator
  Attribute& operator=(Attribute&& rhs) noexcept {
    TUSDZ_LOG_I("Attribute move assignment operator called");
    if (this != &rhs) {
      _name = std::move(rhs._name);
      _variability = rhs._variability;
      _varying_authored = rhs._varying_authored;
      _type_name = std::move(rhs._type_name);
      _var = std::move(rhs._var);
      _paths = std::move(rhs._paths);
      delete _metas;
      _metas = rhs._metas;
      rhs._metas = nullptr;
      rhs._variability = Variability::Varying;
      rhs._varying_authored = false;
    }
    return *this;
  }

  ///
  /// Construct Attribute with typed value(`float`, `token`, ...).
  ///
  template <typename T>
  Attribute(const T &v, bool varying = true) {
    static_assert((value::TypeId::TYPE_ID_VALUE_BEGIN <=
                   value::TypeTraits<T>::type_id()) &&
                      (value::TypeId::TYPE_ID_VALUE_END >
                       value::TypeTraits<T>::type_id()),
                  "T is not a value type");
    set_value(v);
    variability() = varying ? Variability::Varying : Variability::Uniform;
  }

  ///
  /// Construct uniform attribute.
  ///
  template <typename T>
  static Attribute Uniform(const T &v) {

    static_assert((value::TypeId::TYPE_ID_VALUE_BEGIN <=
                   value::TypeTraits<T>::type_id()) &&
                      (value::TypeId::TYPE_ID_VALUE_END >
                       value::TypeTraits<T>::type_id()),
                  "T is not a value type");

    Attribute attr;
    attr.set_value(v);
    attr.variability() = Variability::Uniform;
    return attr;
  }


  ///
  /// Construct connection attribute.
  ///
  Attribute(const Path &v);

  Attribute(const std::vector<Path> &vs);

  const std::string &name() const { return _name; }

  std::string &name() { return _name; }

  void set_name(const std::string &name) { _name = name; }

  void set_type_name(const std::string &tname) { _type_name = tname; }

  // `var` may be empty or ValueBlock, so store type info with set_type_name and
  // set_type_id.
  std::string type_name() const;

  uint32_t type_id() const;

  template <typename T>
  void set_value(const T &v) {
    if (_type_name.empty()) {
      _type_name = value::TypeTraits<T>::type_name();
    }
    _var.set_value(v);
  }

  template <typename T>
  void set_value(T &&v) {
    if (_type_name.empty()) {
      _type_name = value::TypeTraits<T>::type_name();
    }
    _var.set_value(std::move(v));
  }

  void set_var(primvar::PrimVar &v) {
    if (_type_name.empty()) {
      _type_name = v.type_name();
    }

    _var = v;
  }

  void set_var(primvar::PrimVar &&v) {
    if (_type_name.empty()) {
      _type_name = v.type_name();
    }

    _var = std::move(v);
  }

  bool is_value() const;

  // check if Attribute has default value
  bool has_value() const {
    return _var.has_value(); 
  }

  /// @brief Get the value of Attribute of specified type.
  /// @tparam T value type
  /// @return The value if the underlying PrimVar is type T. Return
  /// nonstd::nullpt when type mismatch.
  template <typename T>
  nonstd::optional<T> get_value() const {
    return _var.get_value<T>();
  }

  template <typename T>
  bool get_value(T *v) const {
    if (!v) {
      return false;
    }

    nonstd::optional<T> ret = _var.get_value<T>();
    if (ret) {
      (*v) = std::move(ret.value());
      return true;
    }

    return false;
  }

  template <typename T>
  void set_timesample(const T &v, double t) {
    _var.set_timesample(t, v);
  }

  template <typename T>
  bool get(const double t, T *dst,
           value::TimeSampleInterpolationType tinterp =
           value::TimeSampleInterpolationType::Linear) const;

  // TODO: Deprecate 'get_value' API
  template <typename T>
  bool get_value(const double t, T *dst,
                 value::TimeSampleInterpolationType tinterp =
                     value::TimeSampleInterpolationType::Linear) const {
    return get(t, dst, tinterp);
  }

  /// @brief Get TypedArrayView to the underlying array data of this Attribute.
  /// 
  /// Returns a zero-copy view over array data for scalar (default) values only.
  /// This method does NOT support timesamples - only works with default values.
  /// For non-array types or timesamples, returns an empty view.
  ///
  /// The view provides efficient access to array data without copying, enabling
  /// memory-optimized processing of vertex attributes, indices, and other array data.
  ///
  /// @tparam T The desired element type for the view
  /// @param strict_cast If true, requires exact type match; if false, allows compatible role type casting
  /// @return TypedArrayView<const T> - may be empty if type conversion not possible or attribute has timesamples
  ///
  /// Example:
  /// ```cpp
  /// // Get view of vertex positions as float3
  /// auto positions_view = position_attr.get_value_view<value::float3>();
  /// if (!positions_view.empty()) {
  ///   for (const auto& pos : positions_view) {
  ///     // Process position without copying data
  ///   }
  /// }
  ///
  /// // Get view as compatible role type
  /// auto normals_view = normal_attr.get_value_view<value::vector3f>();  // float3 -> vector3f
  /// ```
  template <typename T>
  TypedArrayView<const T> get_value_view(bool strict_cast = false) const;

  /// @brief Mutable version of get_value_view() for write access to array data.
  ///
  /// Same as get_value_view() but returns a mutable view that allows modification
  /// of the underlying array data. Only works with scalar (default) values.
  ///
  /// @tparam T The desired element type for the view  
  /// @param strict_cast If true, requires exact type match; if false, allows compatible role type casting
  /// @return TypedArrayView<T> - may be empty if type conversion not possible or attribute has timesamples
  ///
  /// Example:
  /// ```cpp
  /// // Get mutable view and modify data in-place
  /// auto positions_view = position_attr.get_value_view<value::float3>();
  /// if (!positions_view.empty()) {
  ///   positions_view[0] = {1.0f, 2.0f, 3.0f};  // Modifies original data
  /// }
  /// ```
  template <typename T>
  TypedArrayView<T> get_value_view(bool strict_cast = false);


  const AttrMeta &metas() const;
  AttrMeta &metas();

  const primvar::PrimVar &get_var() const { return _var; }
  primvar::PrimVar &get_var() { return _var; }

  void set_blocked(bool onoff) { _var.set_blocked(onoff); }

  bool is_blocked() const;
  bool has_blocked() const { return _var.is_blocked(); }

  Variability &variability() { return _variability; }
  Variability variability() const { return _variability; }

  bool is_uniform() const { return _variability == Variability::Uniform; }

  void set_varying_authored() { _varying_authored = true; }

  bool is_varying_authored() const { return _varying_authored; }

  bool is_connection() const;

  bool has_connections() const {
    return _paths.size() > 0;
  }


  bool has_default() const {
    return has_value();
  }

  bool is_timesamples() const;

  bool has_timesamples() const {
    return _var.has_timesamples();
  }

  void set_connection(const Path &path);
  void set_connections(const std::vector<Path> &paths);

  nonstd::optional<Path> get_connection() const;

  const std::vector<Path> &connections() const { return _paths; }
  std::vector<Path> &connections() { return _paths; }

  ///
  /// Estimate memory usage of this Attribute in bytes
  ///
  size_t estimate_memory_usage() const;

 private:
  std::string _name;  // attrib name
  Variability _variability{
      Variability::Varying};  // 'uniform` qualifier is handled with
                              // `variability=uniform`

  // `varying` keyword is explicitly specified?
  bool _varying_authored{false};

  // bool _blocked{false};       // Attribute Block('None')
  std::string _type_name;
  primvar::PrimVar _var;
  std::vector<Path> _paths;
  AttrMeta* _metas{nullptr};  // Using pointer to avoid incomplete type issues
};

///
/// Template attribute classes with type safety
///
/// Attribute with type information.
/// For attribute with `uniform` qualifier or TimeSamples, but don't have
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
  static std::string type_name() { return value::TypeTraits<T>::type_name(); }

  static uint32_t type_id() { return value::TypeTraits<T>::type_id(); }

  TypedAttribute() = default;

  TypedAttribute &operator=(const T &value) {
    _attrib = value;

    return (*this);
  }

  // 'default' value or timeSampled value(when T = Animatable)
  void set_value(const T &v) { _attrib = v; }
  bool has_value() const { return _attrib.has_value(); }

  const nonstd::optional<T> get_value() const {
    return _attrib;
  }

  bool get_value(T *dst) const {
    if (!dst) return false;

    if (_attrib) {
      (*dst) = _attrib.value();
      return true;
    }
    return false;
  }

  bool is_blocked() const { return _blocked; }

  // for `uniform` attribute only
  void set_blocked(bool onoff) { _blocked = onoff; }

  bool is_connection() const { return _paths.size() && !has_value(); }

  void set_connection(const Path &path) {
    _paths.clear();
    _paths.push_back(path);
  }

  void set_connections(const std::vector<Path> &paths) { _paths = paths; }

  const std::vector<Path> &get_connections() const { return _paths; }
  const std::vector<Path> &connections() const { return _paths; }

  const nonstd::optional<Path> get_connection() const {
    if (_paths.size()) {
      return _paths[0];
    }

    return nonstd::nullopt;
  }

  bool has_connections() const {
    return _paths.size();
  }

  void clear_connections() {
    _paths.clear();
  }

  // TODO: Supply set_connection_empty()?

  void set_value_empty() { _value_empty = true; }

  //
  // Check if the attribute is authored, but no value(including ValueBlock) assigned.
  // e.g.
  //
  // float myval;
  //
  bool is_value_empty() const {
    if (has_connections()) {
      return false;
    }

    if (_attrib.has_value()) {
      return false;
    }

    if (_blocked) {
      return false;
    }

    return _value_empty;
  }

  // The attribute authroed?
  bool authored() const {
    if (_attrib) {
      return true;
    }

    if (has_connections()) {
      return true;
    }

    if (_value_empty) {
      // Declare only.
      return true;
    }

    if (_blocked) {
      return true;
    }

    return false;
  }

  void clear_value() {
    _attrib.reset();
    _value_empty = true;
  }

  const AttrMeta &metas() const { return _metas; }
  AttrMeta &metas() { return _metas; }

 private:
  AttrMeta _metas;
  bool _value_empty{false};  // applies `_attrib`
  std::vector<Path> _paths;
  nonstd::optional<T> _attrib;
  bool _blocked{false};
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
  void set_authored(bool onoff) { _authored = onoff; }

  // value set?
  bool authored() const { return _authored; }

  static std::string type_name() { return value::TypeTraits<T>::type_name(); }
  static uint32_t type_id() { return value::TypeTraits<T>::type_id(); }

  // Actual type is a typeName in USDA or USDC
  // for example, we accect float3 type for TypedTerminalAttribute<color3f> and
  // print/serialize this attribute value with actual type.
  //
  void set_actual_type_name(const std::string &type_name) {
    _actual_type_name = type_name;
  }

  bool has_actual_type() const { return _actual_type_name.size(); }

  const std::string &get_actual_type_name() const { return _actual_type_name; }

  const AttrMeta &metas() const { return _metas; }
  AttrMeta &metas() { return _metas; }

 private:
  AttrMeta _metas;
  bool _authored{false};
  std::string _actual_type_name;
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
  static std::string type_name() { return value::TypeTraits<T>::type_name(); }
  static uint32_t type_id() { return value::TypeTraits<T>::type_id(); }

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

  void set_value(const T &v) { _attrib = v; }

  void set_value_empty() { _empty = true; }

  bool has_connections() const { return _paths.size(); }

  //
  // Check if the attribute is authored, but no value(including ValueBlock) assigned.
  // e.g.
  //
  // float myval;
  //
  bool is_value_empty() const {
    if (has_connections()) {
      return false;
    }

    if (_empty) {
      return true;
    }

    if (_attrib) {
      return false;
    }

    return true;
  }

  bool has_value() const {
    if (_empty) {
      return false;
    }

    return true;
  }

  const T &get_value() const {
    if (_attrib) {
      return _attrib.value();
    }
    return _fallback;
  }

  bool is_blocked() const { return _blocked; }

  // for `uniform` attribute only
  void set_blocked(bool onoff) { _blocked = onoff; }

  bool is_connection() const { return _paths.size() && !has_value() ; }

  void set_connection(const Path &path) {
    _paths.clear();
    _paths.push_back(path);
  }

  void set_connections(const std::vector<Path> &paths) { _paths = paths; }

  const std::vector<Path> &get_connections() const { return _paths; }
  const std::vector<Path> &connections() const { return _paths; }

  const nonstd::optional<Path> get_connection() const {
    if (_paths.size()) {
      return _paths[0];
    }

    return nonstd::nullopt;
  }

  void clear_connections() { _paths.clear(); }

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

  const AttrMeta &metas() const { return _metas; }
  AttrMeta &metas() { return _metas; }

 private:
  AttrMeta _metas;
  std::vector<Path> _paths;
  nonstd::optional<T> _attrib;
  bool _empty{false};
  T _fallback;
  bool _blocked{false};  // for `uniform` attribute.
};

template <typename T>
using TypedAnimatableAttributeWithFallback =
    TypedAttributeWithFallback<Animatable<T>>;

//#ifndef TINYUSDZ_INSIDE_PRIM_TYPES
}  // namespace tinyusdz
//#endif

