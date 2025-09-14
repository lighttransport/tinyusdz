// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
#include <algorithm>
#include <cstdio>
#include <limits>
#include <numeric>
//
#include "prim-types.hh"
#include "dictionary.hh"
#include "str-util.hh"
#include "tiny-format.hh"
//
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
//
#include "common-macros.inc"
#include "pprinter.hh"
#include "value-pprint.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

//#include "external/pystring.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// PushError macro removed - Layer implementation moved to layer.cc

namespace tinyusdz {

template<class InputIt1, class InputIt2>
bool lexicographical_compare(InputIt1 first1, InputIt1 last1,
                             InputIt2 first2, InputIt2 last2)
{
    for (; (first1 != last1) && (first2 != last2); ++first1, (void) ++first2)
    {
        if (*first1 < *first2)
            return true;
        if (*first2 < *first1)
            return false;
    }
 
    return (first1 == last1) && (first2 != last2);
}

nonstd::optional<Interpolation> InterpolationFromString(const std::string &v) {
  if ("faceVarying" == v) {
    return Interpolation::FaceVarying;
  } else if ("constant" == v) {
    return Interpolation::Constant;
  } else if ("uniform" == v) {
    return Interpolation::Uniform;
  } else if ("vertex" == v) {
    return Interpolation::Vertex;
  } else if ("varying" == v) {
    return Interpolation::Varying;
  }
  return nonstd::nullopt;
}

nonstd::optional<Orientation> OrientationFromString(const std::string &v) {
  if ("rightHanded" == v) {
    return Orientation::RightHanded;
  } else if ("leftHanded" == v) {
    return Orientation::LeftHanded;
  }
  return nonstd::nullopt;
}

bool operator==(const Path &lhs, const Path &rhs) {
  if (!lhs.is_valid()) {
    return false;
  }

  if (!rhs.is_valid()) {
    return false;
  }

  // Currently simply compare string.
  // FIXME: Better Path identity check.
  return (lhs.full_path_name() == rhs.full_path_name());
}

bool ConvertTokenAttributeToStringAttribute(
    const TypedAttribute<Animatable<value::token>> &inp,
    TypedAttribute<Animatable<std::string>> &out) {
  
    out.metas() = inp.metas();
  
    if (inp.is_blocked()) {
      out.set_blocked(true);
    } else if (inp.is_value_empty()) {
      out.set_value_empty();
    } else if (inp.is_connection()) {
      out.set_connections(inp.get_connections());
    } else {
      Animatable<value::token> toks;
      Animatable<std::string> strs;
      if (inp.get_value(&toks)) {
        if (toks.is_scalar()) {
          value::token tok;
          toks.get_scalar(&tok);
          strs.set(tok.str());
        } else if (toks.is_timesamples()) {
          auto tok_ts = toks.get_timesamples();
  
          for (auto &item : tok_ts.get_samples()) {
            strs.add_sample(item.t, item.value.str());
          }
        } else if (toks.is_blocked()) {
          // TODO
          return false;
        }
      }
      out.set_value(strs);
    }
  
    return true;
  }
  



nonstd::optional<Kind> KindFromString(const std::string &str) {
  if (str == "model") {
    return Kind::Model;
  } else if (str == "group") {
    return Kind::Group;
  } else if (str == "assembly") {
    return Kind::Assembly;
  } else if (str == "component") {
    return Kind::Component;
  } else if (str == "subcomponent") {
    return Kind::Subcomponent;
  } else if (str == "sceneLibrary") {
    // https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/scenelibrary
    return Kind::SceneLibrary;
  } else if (str.empty()) {
    return nonstd::nullopt;
  } else {
    return Kind::UserDef;
  }
}

bool ValidatePrimElementName(const std::string &name) {
  if (name.empty()) {
    return false;
  }

  // alphanum + '_'
  // first char must not be number.

  if (std::isdigit(int(name[0]))) {
    return false;
  } else if (std::isalpha(int(name[0]))) {
    // ok
  } else if (name[0] == '_') {
    // ok
  } else {
    return false;
  }

  for (size_t i = 1; i < name.size(); i++) {
    if (std::isalnum(int(name[i])) || (name[i] == '_')) {
      // ok
    } else {
      return false;
    }
  }

  return true;
}

//
// -- Prim
//

namespace {

const PrimMeta *GetPrimMeta(const value::Value &v) {
  // Lookup PrimMeta variable in Prim class

#define GET_PRIM_META(__ty)       \
  if (v.as<__ty>()) {             \
    return &(v.as<__ty>()->meta); \
  }

  GET_PRIM_META(Model)
  GET_PRIM_META(Scope)
  GET_PRIM_META(Xform)
  GET_PRIM_META(GPrim)
  GET_PRIM_META(GeomMesh)
  GET_PRIM_META(GeomPoints)
  GET_PRIM_META(GeomCube)
  GET_PRIM_META(GeomCapsule)
  GET_PRIM_META(GeomCylinder)
  GET_PRIM_META(GeomSphere)
  GET_PRIM_META(GeomCone)
  GET_PRIM_META(GeomSubset)
  GET_PRIM_META(GeomCamera)
  GET_PRIM_META(GeomBasisCurves)
  GET_PRIM_META(DomeLight)
  GET_PRIM_META(SphereLight)
  GET_PRIM_META(CylinderLight)
  GET_PRIM_META(DiskLight)
  GET_PRIM_META(DistantLight)
  GET_PRIM_META(RectLight)
  GET_PRIM_META(Material)
  GET_PRIM_META(Shader)
  // GET_PRIM_META(UsdPreviewSurface)
  // GET_PRIM_META(UsdUVTexture)
  // GET_PRIM_META(UsdPrimvarReader_int)
  // GET_PRIM_META(UsdPrimvarReader_float)
  // GET_PRIM_META(UsdPrimvarReader_float2)
  // GET_PRIM_META(UsdPrimvarReader_float3)
  // GET_PRIM_META(UsdPrimvarReader_float4)
  GET_PRIM_META(SkelRoot)
  GET_PRIM_META(Skeleton)
  GET_PRIM_META(SkelAnimation)
  GET_PRIM_META(BlendShape)

#undef GET_PRIM_META

  return nullptr;
}

PrimMeta *GetPrimMeta(value::Value &v) {
  // Lookup PrimMeta variable in Prim class

#define GET_PRIM_META(__ty)       \
  if (v.as<__ty>()) {             \
    return &(v.as<__ty>()->meta); \
  }

  GET_PRIM_META(Model)
  GET_PRIM_META(Scope)
  GET_PRIM_META(Xform)
  GET_PRIM_META(GPrim)
  GET_PRIM_META(GeomMesh)
  GET_PRIM_META(GeomPoints)
  GET_PRIM_META(GeomCube)
  GET_PRIM_META(GeomCapsule)
  GET_PRIM_META(GeomCylinder)
  GET_PRIM_META(GeomSphere)
  GET_PRIM_META(GeomCone)
  GET_PRIM_META(GeomSubset)
  GET_PRIM_META(GeomCamera)
  GET_PRIM_META(GeomBasisCurves)
  GET_PRIM_META(DomeLight)
  GET_PRIM_META(SphereLight)
  GET_PRIM_META(CylinderLight)
  GET_PRIM_META(DiskLight)
  GET_PRIM_META(DistantLight)
  GET_PRIM_META(RectLight)
  GET_PRIM_META(Material)
  GET_PRIM_META(Shader)
  // GET_PRIM_META(UsdPreviewSurface)
  // GET_PRIM_META(UsdUVTexture)
  // GET_PRIM_META(UsdPrimvarReader_int)
  // GET_PRIM_META(UsdPrimvarReader_float)
  // GET_PRIM_META(UsdPrimvarReader_float2)
  // GET_PRIM_META(UsdPrimvarReader_float3)
  // GET_PRIM_META(UsdPrimvarReader_float4)
  GET_PRIM_META(SkelRoot)
  GET_PRIM_META(Skeleton)
  GET_PRIM_META(SkelAnimation)
  GET_PRIM_META(BlendShape)

#undef GET_PRIM_META

  return nullptr;
}

}  // namespace

///
/// Stage
///

//
// TODO: Move to prim-types.cc
//

nonstd::optional<std::string> GetPrimElementName(const value::Value &v) {
  // Since multiple get_value() call consumes lots of stack size(depends on
  // sizeof(T)?), Following code would produce 100KB of stack in debug build. So
  // use as() instead(as() => roughly 2000 bytes for stack size).
#if 0
  //
  // TODO: Find a better C++ way... use a std::function?
  //
  if (auto pv = v.get_value<Model>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<Scope>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<Xform>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GPrim>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomMesh>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomBasisCurves>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomSphere>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCube>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCylinder>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCapsule>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCone>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomSubset>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<GeomCamera>()) {
    return Path(pv.value().name, "");
  }

  if (auto pv = v.get_value<DomeLight>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<SphereLight>()) {
    return Path(pv.value().name, "");
  }
  // if (auto pv = v.get_value<CylinderLight>()) { return
  // Path(pv.value().name); } if (auto pv = v.get_value<DiskLight>()) {
  // return Path(pv.value().name); }

  if (auto pv = v.get_value<Material>()) {
    return Path(pv.value().name, "");
  }
  if (auto pv = v.get_value<Shader>()) {
    return Path(pv.value().name, "");
  }
  // if (auto pv = v.get_value<UVTexture>()) { return Path(pv.value().name); }
  // if (auto pv = v.get_value<PrimvarReader()) { return Path(pv.value().name);
  // }

  return nonstd::nullopt;
#else

  // Lookup name field of Prim class

#define EXTRACT_NAME_AND_RETURN_PATH(__ty) \
  if (v.as<__ty>()) {                      \
    return v.as<__ty>()->name;             \
  } else

  EXTRACT_NAME_AND_RETURN_PATH(Model)
  EXTRACT_NAME_AND_RETURN_PATH(Scope)
  EXTRACT_NAME_AND_RETURN_PATH(Xform)
  EXTRACT_NAME_AND_RETURN_PATH(GPrim)
  EXTRACT_NAME_AND_RETURN_PATH(GeomMesh)
  EXTRACT_NAME_AND_RETURN_PATH(GeomPoints)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCube)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCapsule)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCylinder)
  EXTRACT_NAME_AND_RETURN_PATH(GeomSphere)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCone)
  EXTRACT_NAME_AND_RETURN_PATH(GeomSubset)
  EXTRACT_NAME_AND_RETURN_PATH(GeomCamera)
  EXTRACT_NAME_AND_RETURN_PATH(GeomBasisCurves)
  EXTRACT_NAME_AND_RETURN_PATH(DomeLight)
  EXTRACT_NAME_AND_RETURN_PATH(SphereLight)
  EXTRACT_NAME_AND_RETURN_PATH(CylinderLight)
  EXTRACT_NAME_AND_RETURN_PATH(DiskLight)
  EXTRACT_NAME_AND_RETURN_PATH(DistantLight)
  EXTRACT_NAME_AND_RETURN_PATH(RectLight)
  EXTRACT_NAME_AND_RETURN_PATH(Material)
  EXTRACT_NAME_AND_RETURN_PATH(Shader)
  // TODO: extract name must be handled in Shader class
  EXTRACT_NAME_AND_RETURN_PATH(UsdPreviewSurface)
  EXTRACT_NAME_AND_RETURN_PATH(UsdUVTexture)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_int)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float2)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float3)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float4)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_string)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_normal)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_vector)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_point)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_matrix)
  //
  EXTRACT_NAME_AND_RETURN_PATH(SkelRoot)
  EXTRACT_NAME_AND_RETURN_PATH(Skeleton)
  EXTRACT_NAME_AND_RETURN_PATH(SkelAnimation)
  EXTRACT_NAME_AND_RETURN_PATH(BlendShape) { return nonstd::nullopt; }

#undef EXTRACT_NAME_AND_RETURN_PATH

#endif
}

bool SetPrimElementName(value::Value &v, const std::string &elementName) {
  // Lookup name field of Prim class
  bool ok{false};

#define SET_ELEMENT_NAME(__name, __ty) \
  if (v.as<__ty>()) {                  \
    v.as<__ty>()->name = __name;       \
    ok = true;                         \
  } else

  SET_ELEMENT_NAME(elementName, Model)
  SET_ELEMENT_NAME(elementName, Scope)
  SET_ELEMENT_NAME(elementName, Xform)
  SET_ELEMENT_NAME(elementName, GPrim)
  SET_ELEMENT_NAME(elementName, GeomMesh)
  SET_ELEMENT_NAME(elementName, GeomPoints)
  SET_ELEMENT_NAME(elementName, GeomCube)
  SET_ELEMENT_NAME(elementName, GeomCapsule)
  SET_ELEMENT_NAME(elementName, GeomCylinder)
  SET_ELEMENT_NAME(elementName, GeomSphere)
  SET_ELEMENT_NAME(elementName, GeomCone)
  SET_ELEMENT_NAME(elementName, GeomSubset)
  SET_ELEMENT_NAME(elementName, GeomCamera)
  SET_ELEMENT_NAME(elementName, GeomBasisCurves)
  SET_ELEMENT_NAME(elementName, DomeLight)
  SET_ELEMENT_NAME(elementName, SphereLight)
  SET_ELEMENT_NAME(elementName, CylinderLight)
  SET_ELEMENT_NAME(elementName, DiskLight)
  SET_ELEMENT_NAME(elementName, RectLight)
  SET_ELEMENT_NAME(elementName, Material)
  SET_ELEMENT_NAME(elementName, Shader)
  // TODO: set element name must be handled in Shader class
  SET_ELEMENT_NAME(elementName, UsdPreviewSurface)
  SET_ELEMENT_NAME(elementName, UsdUVTexture)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_int)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float2)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float3)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float4)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_string)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_normal)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_vector)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_point)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_matrix)
  //
  SET_ELEMENT_NAME(elementName, SkelRoot)
  SET_ELEMENT_NAME(elementName, Skeleton)
  SET_ELEMENT_NAME(elementName, SkelAnimation)
  SET_ELEMENT_NAME(elementName, BlendShape) { return false; }

#undef SET_ELEMENT_NAME

  return ok;
}

Prim::Prim(const value::Value &rhs) {
  // Check if type is Prim(Model(GPrim), usdShade, usdLux, etc.)
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= rhs.type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > rhs.type_id())) {
    if (auto pv = GetPrimElementName(rhs)) {
      _path = Path(pv.value(), /* prop part*/ "");
      _elementPath = Path(pv.value(), /* prop part */ "");
    }

    _data = rhs;
  } else {
    // TODO: Raise an error if rhs is not an Prim
  }
}

Prim::Prim(value::Value &&rhs) {
  // Check if type is Prim(Model(GPrim), usdShade, usdLux, etc.)
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= rhs.type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > rhs.type_id())) {
    _data = std::move(rhs);

    if (auto pv = GetPrimElementName(_data)) {
      _path = Path(pv.value(), "");
      _elementPath = Path(pv.value(), "");
    }

  } else {
    // TODO: Raise an error if rhs is not an Prim
  }
}

Prim::Prim(const std::string &elementPath, const value::Value &rhs) {
  // Check if type is Prim(Model(GPrim), usdShade, usdLux, etc.)
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= rhs.type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > rhs.type_id())) {
    _path = Path(elementPath, /* prop part*/ "");
    _elementPath = Path(elementPath, /* prop part */ "");

    _data = rhs;
    SetPrimElementName(_data, elementPath);
  } else {
    // TODO: Raise an error if rhs is not an Prim
  }
}

Prim::Prim(const std::string &elementPath, value::Value &&rhs) {
  // Check if type is Prim(Model(GPrim), usdShade, usdLux, etc.)
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= rhs.type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > rhs.type_id())) {
    _path = Path(elementPath, /* prop part */ "");
    _elementPath = Path(elementPath, /* prop part */ "");

    _data = std::move(rhs);
    SetPrimElementName(_data, elementPath);
  } else {
    // TODO: Raise an error if rhs is not an Prim
  }
}

bool Prim::add_child(Prim &&rhs, const bool rename_prim_name,
                     std::string *err) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // TODO: Only take a lock when dirty.
  std::lock_guard<std::mutex> lock(_mutex);
#endif

  std::string elementName = rhs.element_name();

  if (elementName.empty()) {
    if (rename_prim_name) {
      // assign default name `default`
      elementName = "default";

      if (!SetPrimElementName(rhs.get_data(), elementName)) {
        if (err) {
          (*err) = fmt::format(
              "Internal error. cannot modify Prim's elementName.\n");
        }
        return false;
      }
      rhs.element_path() = Path(elementName, /* prop_part */ "");
    } else {
      if (err) {
        (*err) = "Prim has empty elementName.\n";
      }
      return false;
    }
  }

  if (_children.size() != _childrenNameSet.size()) {
    // Rebuild _childrenNames
    _childrenNameSet.clear();
    for (size_t i = 0; i < _children.size(); i++) {
      if (_children[i].element_name().empty()) {
        if (err) {
          (*err) =
              "Internal error: Existing child Prim's elementName is empty.\n";
        }
        return false;
      }

      if (_childrenNameSet.count(_children[i].element_name())) {
        if (err) {
          (*err) =
              "Internal error: _children contains Prim with same "
              "elementName.\n";
        }
        return false;
      }

      _childrenNameSet.insert(_children[i].element_name());
    }
  }

  DCOUT("elementName = " << elementName);

  if (_childrenNameSet.count(elementName)) {
    if (rename_prim_name) {
      std::string unique_name;
      if (!makeUniqueName(_childrenNameSet, elementName, &unique_name)) {
        if (err) {
          (*err) = fmt::format(
              "Internal error. cannot assign unique name for `{}`.\n",
              elementName);
        }
        return false;
      }

      // Ensure valid Prim name
      if (!ValidatePrimElementName(unique_name)) {
        if (err) {
          (*err) = fmt::format(
              "Internally generated Prim name `{}` is invalid as a Prim "
              "name.\n",
              unique_name);
        }
        return false;
      }

      elementName = unique_name;

      // Need to modify both Prim::data::name and Prim::elementPath
      DCOUT("elementName = " << elementName);
      if (!SetPrimElementName(rhs.get_data(), elementName)) {
        if (err) {
          (*err) = fmt::format(
              "Internal error. cannot modify Prim's elementName.\n");
        }
        return false;
      }
      rhs.element_path() = Path(elementName, /* prop_part */ "");
    } else {
      if (err) {
        (*err) = fmt::format(
            "Prim name(elementName) {} already exists in children.\n",
            rhs.element_name());
      }
      return false;
    }
  }

  DCOUT("rhs.elementName = " << rhs.element_name());

  _childrenNameSet.insert(elementName);
  _children.emplace_back(std::move(rhs));
  _child_dirty = true;

  return true;
}

bool Prim::replace_child(const std::string &child_prim_name, Prim &&rhs,
                         std::string *err) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // TODO: Only take a lock when dirty.
  std::lock_guard<std::mutex> lock(_mutex);
#endif

  if (child_prim_name.empty()) {
    if (err) {
      (*err) += "child_prim_name is empty.\n";
    }
  }

  if (!ValidatePrimElementName(child_prim_name)) {
    if (err) {
      (*err) +=
          fmt::format("`{}` is not a valid Prim name.\n", child_prim_name);
    }
  }

  if (_children.size() != _childrenNameSet.size()) {
    // Rebuild _childrenNames
    _childrenNameSet.clear();
    for (size_t i = 0; i < _children.size(); i++) {
      if (_children[i].element_name().empty()) {
        if (err) {
          (*err) =
              "Internal error: Existing child Prim's elementName is empty.\n";
        }
        return false;
      }

      if (_childrenNameSet.count(_children[i].element_name())) {
        if (err) {
          (*err) =
              "Internal error: _children contains Prim with same "
              "elementName.\n";
        }
        return false;
      }

      _childrenNameSet.insert(_children[i].element_name());
    }
  }

  // Simple linear scan
  auto result = std::find_if(_children.begin(), _children.end(),
                             [child_prim_name](const Prim &p) {
                               return (p.element_name() == child_prim_name);
                             });

  if (result != _children.end()) {
    // Need to modify both Prim::data::name and Prim::elementPath
    if (!SetPrimElementName(rhs.get_data(), child_prim_name)) {
      if (err) {
        (*err) =
            fmt::format("Internal error. cannot modify Prim's elementName.\n");
      }
      return false;
    }
    rhs.element_path() = Path(child_prim_name, /* prop_part */ "");

    (*result) = std::move(rhs);  // replace

  } else {
    // Need to modify both Prim::data::name and Prim::elementPath
    if (!SetPrimElementName(rhs.get_data(), child_prim_name)) {
      if (err) {
        (*err) =
            fmt::format("Internal error. cannot modify Prim's elementName.\n");
      }
      return false;
    }
    rhs.element_path() = Path(child_prim_name, /* prop_part */ "");

    _childrenNameSet.insert(child_prim_name);
    _children.emplace_back(std::move(rhs));  // add
  }

  _child_dirty = true;

  return true;
}

const std::vector<int64_t> &Prim::get_child_indices_from_primChildren(
    bool force_update, bool *indices_is_valid) const {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // TODO: Only take a lock when dirty.
  std::lock_guard<std::mutex> lock(_mutex);
#endif

  if (!force_update && (_primChildrenIndices.size() == _children.size()) &&
      !_child_dirty) {
    // got cache.
    if (indices_is_valid) {
      (*indices_is_valid) = _primChildrenIndicesIsValid;
    }
    return _primChildrenIndices;
  }

  if (!force_update) {
    _child_dirty = false;
  }

  if (metas().primChildren.empty()) {
    _primChildrenIndices.resize(_children.size());
    std::iota(_primChildrenIndices.begin(), _primChildrenIndices.end(), 0);
    _primChildrenIndicesIsValid = true;
    if (indices_is_valid) {
      (*indices_is_valid) = _primChildrenIndicesIsValid;
    }
    return _primChildrenIndices;
  }

  std::map<std::string, size_t> m;  // name -> children() index map
  for (size_t i = 0; i < _children.size(); i++) {
    m.emplace(_children[i].element_name(), i);
  }
  std::set<size_t> table;  // to check uniqueness

  // Use the length of primChildren.
  _primChildrenIndices.resize(metas().primChildren.size());

  bool valid = true;

  for (size_t i = 0; i < _primChildrenIndices.size(); i++) {
    std::string tok = metas().primChildren[i].str();
    const auto it = m.find(tok);
    if (it != m.end()) {
      _primChildrenIndices[i] = int64_t(it->second);

      table.insert(it->second);
    } else {
      // Prim name not found.
      _primChildrenIndices[i] = -1;
      valid = false;
    }
  }

  if (table.size() != _primChildrenIndices.size()) {
    // duplicated index exists.
    valid = false;
  }

  _primChildrenIndicesIsValid = valid;
  if (indices_is_valid) {
    (*indices_is_valid) = _primChildrenIndicesIsValid;
  }

  return _primChildrenIndices;
}

//
// To deal with clang's -Wexit-time-destructors, dynamically allocate buffer for
// PrimMeta.
//
// NOTE: not thread-safe.
//
class EmptyStaticMeta {
 private:
  EmptyStaticMeta() = default;

 public:
  static PrimMeta &GetEmptyStaticMeta() {
    if (!s_meta) {
      s_meta = new PrimMeta();
    }

    return *s_meta;
  }

  ~EmptyStaticMeta() {
    delete s_meta;
    s_meta = nullptr;
  }

 private:
  static PrimMeta *s_meta;
};

PrimMeta *EmptyStaticMeta::s_meta = nullptr;

PrimMeta &Prim::metas() {
  PrimMeta *p = GetPrimMeta(_data);
  if (p) {
    return *p;
  }

  // TODO: This should not happen. report an error.
  return EmptyStaticMeta::GetEmptyStaticMeta();
}

const PrimMeta &Prim::metas() const {
  const PrimMeta *p = GetPrimMeta(_data);
  if (p) {
    return *p;
  }

  // TODO: This should not happen. report an error.
  return EmptyStaticMeta::GetEmptyStaticMeta();
}








bool IsXformablePrim(const Prim &prim) {
  uint32_t tyid = prim.type_id();

  // GeomSubset is not xformable

  switch (tyid) {
    case value::TYPE_ID_GPRIM: {
      return true;
    }
    case value::TYPE_ID_GEOM_XFORM: {
      return true;
    }
    case value::TYPE_ID_GEOM_MESH: {
      return true;
    }
    case value::TYPE_ID_GEOM_BASIS_CURVES: {
      return true;
    }
    case value::TYPE_ID_GEOM_SPHERE: {
      return true;
    }
    case value::TYPE_ID_GEOM_CUBE: {
      return true;
    }
    case value::TYPE_ID_GEOM_CYLINDER: {
      return true;
    }
    case value::TYPE_ID_GEOM_CONE: {
      return true;
    }
    case value::TYPE_ID_GEOM_CAPSULE: {
      return true;
    }
    case value::TYPE_ID_GEOM_POINTS: {
      return true;
    }
    // value::TYPE_ID_GEOM_GEOMSUBSET
    case value::TYPE_ID_GEOM_POINT_INSTANCER: {
      return true;
    }
    case value::TYPE_ID_GEOM_CAMERA: {
      return true;
    }
    case value::TYPE_ID_LUX_DOME: {
      return true;
    }
    case value::TYPE_ID_LUX_CYLINDER: {
      return true;
    }
    case value::TYPE_ID_LUX_SPHERE: {
      return true;
    }
    case value::TYPE_ID_LUX_DISK: {
      return true;
    }
    case value::TYPE_ID_LUX_DISTANT: {
      return true;
    }
    case value::TYPE_ID_LUX_RECT: {
      return true;
    }
    case value::TYPE_ID_LUX_GEOMETRY: {
      return true;
    }
    case value::TYPE_ID_LUX_PORTAL: {
      return true;
    }
    case value::TYPE_ID_LUX_PLUGIN: {
      return true;
    }
    case value::TYPE_ID_SKEL_ROOT: {
      return true;
    }
    case value::TYPE_ID_SKELETON: {
      return true;
    }
    default:
      return false;
  }
}

bool CastToXformable(const Prim &prim, const Xformable **xformable) {
  if (!xformable) {
    return false;
  }

  // __ty = class derived from Xformable.
#define TRY_CAST(__ty)             \
  if (auto pv = prim.as<__ty>()) { \
    (*xformable) = pv;             \
    return true;                   \
  }

  // TODO: Use tydra::ApplyToXformable
  TRY_CAST(GPrim)
  TRY_CAST(Xform)
  TRY_CAST(GeomMesh)
  TRY_CAST(GeomBasisCurves)
  TRY_CAST(GeomCube)
  TRY_CAST(GeomSphere)
  TRY_CAST(GeomCylinder)
  TRY_CAST(GeomCone)
  TRY_CAST(GeomCapsule)
  TRY_CAST(GeomPoints)
  TRY_CAST(GeomPointInstancer)
  TRY_CAST(GeomCamera)
  TRY_CAST(SkelRoot)
  TRY_CAST(Skeleton)
  TRY_CAST(RectLight)
  TRY_CAST(DomeLight)
  TRY_CAST(CylinderLight)
  TRY_CAST(SphereLight)
  TRY_CAST(DiskLight)
  TRY_CAST(DistantLight)
  TRY_CAST(RectLight)
  TRY_CAST(GeometryLight)
  TRY_CAST(PortalLight)
  TRY_CAST(PluginLight)
  TRY_CAST(SkelRoot)
  TRY_CAST(Skeleton)

  return false;
}

value::matrix4d GetLocalTransform(const Prim &prim, bool *resetXformStack,
                                  double t,
                                  value::TimeSampleInterpolationType tinterp) {
  if (!IsXformablePrim(prim)) {
    if (resetXformStack) {
      (*resetXformStack) = false;
    }
    return value::matrix4d::identity();
  }

  // default false
  if (resetXformStack) {
    (*resetXformStack) = false;
  }

  const Xformable *xformable{nullptr};
  if (CastToXformable(prim, &xformable)) {
    if (!xformable) {
      return value::matrix4d::identity();
    }

    value::matrix4d m;
    bool rxs{false};
    nonstd::expected<value::matrix4d, std::string> ret =
        xformable->GetLocalMatrix(t, tinterp, &rxs);
    if (ret) {
      if (resetXformStack) {
        (*resetXformStack) = rxs;
      }
      return ret.value();
    }
  }

  return value::matrix4d::identity();
}


bool AttrMetas::has_colorSpace() const {
  return meta.count("colorSpace");
}

value::token AttrMetas::get_colorSpace() const {
  if (!has_colorSpace()) {
    return value::token();
  }

  const MetaVariable &mv = meta.at("colorSpace");
  value::token tok;
  if (mv.get_value<value::token>(&tok)) {
    return tok;
  }

  return value::token();
}

bool AttrMetas::has_unauthoredValuesIndex() const {
  return meta.count("unauthoredValuesIndex");
}

int AttrMetas::get_unauthoredValuesIndex() const {
  if (!has_unauthoredValuesIndex()) {
    return -1;
  }

  const MetaVariable &mv = meta.at("unauthoredValuesIndex");
  int v;
  if (mv.get_value<int>(&v)) {
    return v;
  }

  return -1;
}

namespace {

// GetPrimSpecAtPathRec function moved to layer.cc

// Helper functions moved to layer.cc

}  // namespace

// All Layer methods moved to layer.cc

// Property::estimate_memory_usage() is now implemented in property.cc
// Relationship::estimate_memory_usage() is now implemented in relationship.cc

// Attribute::estimate_memory_usage() is now implemented in attribute.cc

}  // namespace tinyusdz
