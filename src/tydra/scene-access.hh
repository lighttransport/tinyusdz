// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// Scene access API
//
// NOTE: Tydra API does not use nonstd::optional and nonstd::expected,
// std::functions and other non basic STL feature for easier language bindings.
//
#pragma once

#include <map>

#include "prim-types.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdSkel.hh"

namespace tinyusdz {
namespace tydra {

// key = fully absolute Prim path in string(e.g. "/xform/geom0")
template <typename T>
using PathPrimMap = std::map<std::string, const T *>;

//
// value = pair of Shader Prim which contains the Shader type T("info:id") and
// its concrete Shader type(UsdPreviewSurface)
//
template <typename T>
using PathShaderMap =
    std::map<std::string, std::pair<const Shader *, const T *>>;

///
/// List Prim of type T from the Stage.
/// Returns false when unsupported/unimplemented Prim type T is given.
///
template <typename T>
bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<T> &m /* output */);

///
/// List Shader of shader type T from the Stage.
/// Returns false when unsupported/unimplemented Shader type T is given.
/// TODO: User defined shader type("info:id")
///
template <typename T>
bool ListShaders(const tinyusdz::Stage &stage,
                 PathShaderMap<T> &m /* output */);

///
/// Get parent Prim from Path.
/// Path must be fully expanded absolute path.
///
/// Example: Return "/xform" Prim for "/xform/mesh0" path
///
/// Returns nullptr when the given Path is a root Prim or invalid Path(`err`
/// will be filled when failed).
///
const Prim *GetParentPrim(const tinyusdz::Stage &stage,
                          const tinyusdz::Path &path, std::string *err);

///
/// Visit Stage and invoke callback functions for each Prim.
/// Can be used for alternative method of Stage::Traverse() in pxrUSD
///

///
/// Use old-style Callback function approach for easier language bindings
///
/// @param[in] prim Prim
/// @param[in] tree_depth Tree depth of this Prim. 0 = root prim.
/// @param[inout] userdata User data.
///
/// @return Usually true. false to notify stop visiting Prims further.
///
typedef bool (*VisitPrimFunction)(const Prim &prim, const int32_t tree_depth,
                                  void *userdata);

void VisitPrims(const tinyusdz::Stage &stage, VisitPrimFunction visitor_fun,
                void *userdata = nullptr);

///
/// Get Property(Attribute or Relationship) of given Prim by name.
/// Similar to UsdPrim::GetProperty() in pxrUSD.
///
/// @param[in] prim Prim
/// @param[in] prop_name Property name
/// @param[out] prop Property
/// @param[out] err Error message(filled when returning false)
///
/// @return true if Property found in given Prim.
/// @return false if Property is not found in given Prim.
///
bool GetProperty(const tinyusdz::Prim &prim, const std::string &attr_name,
                 Property *prop, std::string *err);

///
/// Get Attribute of given Prim by name.
/// Similar to UsdPrim::GetAttribute() in pxrUSD.
///
/// @param[in] prim Prim
/// @param[in] attr_name Attribute name
/// @param[out] attr Attribute
/// @param[out] err Error message(filled when returning false)
///
/// @return true if Attribute found in given Prim.
/// @return false if Attribute is not found in given Prim, or `attr_name` is a
/// Relationship.
///
bool GetAttribute(const tinyusdz::Prim &prim, const std::string &attr_name,
                  Attribute *attr, std::string *err);

///
/// Get Relationship of given Prim by name.
/// Similar to UsdPrim::GetRelationship() in pxrUSD.
///
/// @param[in] prim Prim
/// @param[in] rel_name Relationship name
/// @param[out] rel Relationship
/// @param[out] err Error message(filled when returning false)
///
/// @return true if Relationship found in given Prim.
/// @return false if Relationship is not found in given Prim, or `rel_name` is a
/// Attribute.
///
bool GetRelationship(const tinyusdz::Prim &prim, const std::string &rel_name,
                     Relationship *rel, std::string *err);

bool HasRelationship(const tinyusdz::Prim &prim, const std::string &rel_name);

///
/// Terminal Attribute value at specified timecode.
///
/// - No `None`(Value Blocked)
/// - No connection(connection target is evaluated(fetch value producing
/// attribute))
/// - No timeSampled value
///
class TerminalAttributeValue {
 public:
  TerminalAttributeValue() = default;

  TerminalAttributeValue(const value::Value &v) : _empty{false}, _value(v) {}
  TerminalAttributeValue(value::Value &&v)
      : _empty{false}, _value(std::move(v)) {}

  // "empty" attribute(type info only)
  void set_empty_attribute(const std::string &type_name) {
    _empty = true;
    _type_name = type_name;
  }

  TerminalAttributeValue(const std::string &type_name) {
    set_empty_attribute(type_name);
  }

  bool is_empty() const { return _empty; }

  template <typename T>
  const T *as() const {
    if (_empty) {
      return nullptr;
    }
    return _value.as<T>();
  }

  template <typename T>
  bool is() const {
    if (_empty) {
      return false;
    }

    if (_value.as<T>()) {
      return true;
    }
    return false;
  }

  void set_value(const value::Value &v) {
    _value = v;
    _empty = false;
  }

  void set_value(value::Value &&v) {
    _value = std::move(v);
    _empty = false;
  }

  const std::string type_name() const {
    if (_empty) {
      return _type_name;
    }

    return _value.type_name();
  }

  uint32_t type_id() const {
    if (_empty) {
      return value::GetTypeId(_type_name);
    }

    return _value.type_id();
  }

  Variability variability() const { return _variability; }
  Variability &variability() { return _variability; }

  const AttrMeta &meta() const { return _meta; }
  AttrMeta &meta() { return _meta; }

 private:
  bool _empty{true};
  std::string _type_name;
  Variability _variability{Variability::Varying};
  value::Value _value{nullptr};
  AttrMeta _meta;
};

///
/// Evaluate Attribute of the specied Prim and retrieve terminal Attribute
/// value.
///
/// - If the attribute is empty(e.g. `float outputs:r`), return "empty"
/// Attribute
/// - If the attribute is scalar value, simply returns it.
/// - If the attribute is timeSamples value, evaluate the value at specified
/// time.
/// - If the attribute is connection, follow the connection target
///
/// @param[in] stage Stage
/// @param[in] prim Prim
/// @param[in] attr_name Attribute name
/// @param[out] value Evaluated terminal attribute value.
/// @param[out] err Error message(filled when false returned)
/// @param[in] t (optional) TimeCode(for timeSamples Attribute)
/// @param[in] tinterp (optional) Interpolation type for timeSamples value
///
/// Return false when:
///
/// - If the attribute is None(ValueBlock)
/// - Requested attribute not found in a Prim.
/// - Invalid connection(e.g. type mismatch, circular referencing, targetPath
/// points non-existing path, etc),
/// - Other error happens.
///
bool EvaluateAttribute(
    const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, const double t = tinyusdz::value::TimeCode::Default(),
    const tinyusdz::value::TimeSampleInterpolationType tinterp =
        tinyusdz::value::TimeSampleInterpolationType::Held);

//
// For USDZ AR extensions
//

///
/// List up `sceneName` of given Prim's children
/// https://developer.apple.com/documentation/realitykit/usdz-schemas-for-ar
///
/// Prim's Kind must be `sceneLibrary`
/// @param[out] List of pair of (Is Specifier `over`, sceneName). For `def`
/// Specifier(primary scene), it is set to false.
///
///
bool ListSceneNames(const tinyusdz::Prim &root,
                    std::vector<std::pair<bool, std::string>> *sceneNames);

}  // namespace tydra
}  // namespace tinyusdz
