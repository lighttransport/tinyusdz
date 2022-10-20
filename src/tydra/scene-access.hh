// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// Scene access API
//
// NOTE: Tydra API does not use nonstd::optional and nonstd::expected, std::functions and other non basic STL feature for easier language bindings.
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
/// Returns nullptr when the given Path is a root Prim or invalid Path(`err` will be filled when failed).
///
const Prim *GetParentPrim(const tinyusdz::Stage &stage, const tinyusdz::Path &path, std::string *err);


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
typedef bool (*VisitPrimFunction)(const Prim &prim, const int32_t tree_depth, void *userdata);

void VisitPrims(const tinyusdz::Stage &stage, VisitPrimFunction visitor_fun, void *userdata=nullptr);

///
/// Terminal Attribute value at specified timecode.
///
/// - No "empty" value
/// - No connection(connection target is evaluated(fetch value producing attribute))
/// - No timeSampled value
///
class TerminalAttributeValue
{
 public:

  template<typename T>
  const T *as() const {
    return value.as<T>();
  }

  template<typename T>
  bool is() const {
    if (value.as<T>()) {
      return true;
    }
    return false;
  }

 private:
  value::Value value{nullptr};
};

///
/// Evaluate Attribute of the specied Prim and retrieve terminal Attribute value.
///
/// - If the attribute is timeSamples value, evaluate the value at specified time.
/// - If the attribute is connection, follow the connection target 
///
/// @param[in] stage Stage
/// @param[in] prim Prim
/// @param[in] attr_name Attribute name
/// @param[out] value Evaluated terminal attribute value.
/// @param[out] err Error message(filled when false returned)
/// @param[in] tc (optional) TimeCode(for timeSamples Attribute)
/// @param[in] tinterp (optional) Interpolation type for timeSamples value
///
/// Return false when:
///
/// - Requested attribute not found.
/// - Invalid connection(e.g. type mismatch, circular referencing, ...).
/// - Other error happens.
///
bool EvaluateAttribute(
  const tinyusdz::Stage &stage,
  const tinyusdz::Prim &prim,
  const std::string &attr_name,
  TerminalAttributeValue *value,
  std::string *err,
  const tinyusdz::value::TimeCode tc = tinyusdz::value::TimeCode::Default(),
  const tinyusdz::TimeSampleInterpolationType tinterp = tinyusdz::TimeSampleInterpolationType::Held
  );

}  // namespace tydra
}  // namespace tinyusdz
