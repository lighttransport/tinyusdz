// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
#include <algorithm>
#include <limits>
//
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
//
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
//
#include "common-macros.inc"
#include "value-pprint.hh"
#include "pprinter.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

//#include "external/pystring.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

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

//
// -- Path
//

Path::Path(const std::string &p, const std::string &prop) {
  //
  // For absolute path, starts with '/' and no other '/' exists.
  // For property part, '.' exists only once.
  (void)prop;

  if (p.size() < 1) {
    _valid = false;
    return;
  }

  auto slash_fun = [](const char c) { return c == '/'; };
  auto dot_fun = [](const char c) { return c == '.'; };

  // TODO: More checks('{', '[', ...)

  if (p[0] == '/') {
    // absolute path

    auto ndots = std::count_if(p.begin(), p.end(), dot_fun);

    if (ndots == 0) {
      // absolute prim.
      _prim_part = p;
      _valid = true;
    } else if (ndots == 1) {
      if (p.size() < 3) {
        // "/."
        _valid = false;
        return;
      }

      auto loc = p.find_first_of('.');
      if (loc == std::string::npos) {
        // ?
        _valid = false;
        return;
      }

      if (loc <= 0) {
        // this should not happen though.
        _valid = false;
      }

      // split
      std::string prop_name = p.substr(size_t(loc));

      // Check if No '/' in prop_part
      if (std::count_if(prop_name.begin(), prop_name.end(), slash_fun) > 0) {
        _valid = false;
        return;
      }

      _prop_part = prop_name.erase(0, 1);  // remove '.'
      _prim_part = p.substr(0, size_t(loc));

      _valid = true;

    } else {
      _valid = false;
      return;
    }

  } else if (p[0] == '.') {
    // property
    auto nslashes = std::count_if(p.begin(), p.end(), slash_fun);
    if (nslashes > 0) {
      _valid = false;
      return;
    }

    _prop_part = p;
    _prop_part = _prop_part.erase(0, 1);
    _valid = true;

  } else {
    // prim.prop

    auto ndots = std::count_if(p.begin(), p.end(), dot_fun);
    if (ndots == 0) {
      // relative prim.
      _prim_part = p;
      _valid = true;
    } else if (ndots == 1) {
      if (p.size() < 3) {
        // "/."
        _valid = false;
        return;
      }

      auto loc = p.find_first_of('.');
      if (loc == std::string::npos) {
        // ?
        _valid = false;
        return;
      }

      if (loc <= 0) {
        // this should not happen though.
        _valid = false;
      }

      // split
      std::string prop_name = p.substr(size_t(loc));

      // Check if No '/' in prop_part
      if (std::count_if(prop_name.begin(), prop_name.end(), slash_fun) > 0) {
        _valid = false;
        return;
      }

      _prim_part = p.substr(0, size_t(loc));
      _prop_part = prop_name.erase(0, 1);  // remove '.'

      _valid = true;

    } else {
      _valid = false;
      return;
    }
  }
}

Path Path::append_property(const std::string &elem) {
  Path &p = (*this);

  if (elem.empty()) {
    p._valid = false;
    return p;
  }

  if (tokenize_variantElement(elem)) {
    // variant chars are not supported yet.
    p._valid = false;
    return p;
  }

  if (elem[0] == '[') {
    // relational attrib are not supported
    p._valid = false;
    return p;
  } else if (elem[0] == '.') {
    // Relative
    // std::cerr << "???. elem[0] is '.'\n";
    // For a while, make this valid.
    p._valid = false;
    return p;
  } else {
    // TODO: Validate property path.
    p._prop_part = elem;
    p._element = elem;

    return p;
  }
}

const Path Path::AppendPrim(const std::string &elem) const {
  Path p = (*this);  // copies

  p.append_prim(elem);

  return p;
}

const Path Path::AppendProperty(const std::string &elem) const {
  Path p = (*this);  // copies

  p.append_property(elem);

  return p;
}

std::pair<Path, Path> Path::split_at_root() const {
  if (is_absolute_path()) {
    if (is_root_path()) {
      return std::make_pair(Path("/", ""), Path());
    }

    std::string p = full_path_name();

    if (p.size() < 2) {
      // Never should reach here. just in case
      return std::make_pair(*this, Path());
    }

    // Fine 2nd '/'
    auto ret =
        std::find_if(p.begin() + 1, p.end(), [](char c) { return c == '/'; });

    if (ret != p.end()) {
      auto ndist = std::distance(p.begin(), ret);  // distance from str[0]
      if (ndist < 1) {
        // This should not happen though.
        return std::make_pair(*this, Path());
      }
      size_t n = size_t(ndist);
      std::string root = p.substr(0, n);
      std::string siblings = p.substr(n);

      Path rP(root, "");
      Path sP(siblings, "");

      return std::make_pair(rP, sP);
    }

    return std::make_pair(*this, Path());
  } else {
    return std::make_pair(Path(), *this);
  }
}

Path Path::append_element(const std::string &elem) {
  Path &p = (*this);

  if (elem.empty()) {
    p._valid = false;
    return p;
  }

  std::array<std::string, 2> variant;
  if (tokenize_variantElement(elem, &variant)) {
    // variant is not supported yet.
    p._valid = false;
    return p;
  }

  if (elem[0] == '[') {
    // relational attrib are not supported
    p._valid = false;
    return p;
  } else if (elem[0] == '.') {
    // Relative path
    // For a while, make this valid.
    p._valid = false;
    return p;
  } else {
    // std::cout << "elem " << elem << "\n";
    if ((p._prim_part.size() == 1) && (p._prim_part[0] == '/')) {
      p._prim_part += elem;
    } else {
      // TODO: Validate element name.
      p._prim_part += '/' + elem;
    }

    // Also store raw element name
    p._element = elem;

    return p;
  }
}

Path Path::get_parent_prim_path() const {
  if (!_valid) {
    return Path();
  }

  if (is_root_prim()) {
    return *this;
  }

  size_t n = _prim_part.find_last_of('/');
  if (n == std::string::npos) {
    // this should never happen though.
    return Path();
  }

  if (n == 0) {
    // return root
    return Path("/", "");
  }

  return Path(_prim_part.substr(0, n), "");
}

const std::string &Path::element_name() const {
  if (_element.empty()) {
    // Get last item.
    std::vector<std::string> tokenized_prim_names = split(prim_part(), "/");
    if (tokenized_prim_names.size()) {
      _element = tokenized_prim_names[size_t(tokenized_prim_names.size() - 1)];
    }
  }

  return _element;
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
  }
  return nonstd::nullopt;
}

bool ValidatePrimName(const std::string &name) {
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
  GET_PRIM_META(RectLight)
  GET_PRIM_META(Material)
  GET_PRIM_META(Shader)
  GET_PRIM_META(UsdPreviewSurface)
  GET_PRIM_META(UsdUVTexture)
  GET_PRIM_META(UsdPrimvarReader_int)
  GET_PRIM_META(UsdPrimvarReader_float)
  GET_PRIM_META(UsdPrimvarReader_float2)
  GET_PRIM_META(UsdPrimvarReader_float3)
  GET_PRIM_META(UsdPrimvarReader_float4)
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
  GET_PRIM_META(RectLight)
  GET_PRIM_META(Material)
  GET_PRIM_META(Shader)
  GET_PRIM_META(UsdPreviewSurface)
  GET_PRIM_META(UsdUVTexture)
  GET_PRIM_META(UsdPrimvarReader_int)
  GET_PRIM_META(UsdPrimvarReader_float)
  GET_PRIM_META(UsdPrimvarReader_float2)
  GET_PRIM_META(UsdPrimvarReader_float3)
  GET_PRIM_META(UsdPrimvarReader_float4)
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
  EXTRACT_NAME_AND_RETURN_PATH(RectLight)
  EXTRACT_NAME_AND_RETURN_PATH(Material)
  EXTRACT_NAME_AND_RETURN_PATH(Shader)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPreviewSurface)
  EXTRACT_NAME_AND_RETURN_PATH(UsdUVTexture)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_int)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float2)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float3)
  EXTRACT_NAME_AND_RETURN_PATH(UsdPrimvarReader_float4)
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
  SET_ELEMENT_NAME(elementName, UsdPreviewSurface)
  SET_ELEMENT_NAME(elementName, UsdUVTexture)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_int)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float2)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float3)
  SET_ELEMENT_NAME(elementName, UsdPrimvarReader_float4)
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

bool SetCustomDataByKey(const std::string &key, const MetaVariable &var,
                        CustomDataType &custom) {
  // split by namespace
  std::vector<std::string> names = split(key, ":");
  DCOUT("names = " << to_string(names));

  if (names.empty()) {
    DCOUT("names is empty");
    return false;
  }

  if (names.size() > 1024) {
    // too deep
    DCOUT("too deep");
    return false;
  }

  CustomDataType *curr = &custom;

  for (size_t i = 0; i < names.size(); i++) {
    const std::string &elemkey = names[i];
    DCOUT("elemkey = " << elemkey);

    if (i == (names.size() - 1)) {
      DCOUT("leaf");
      // leaf
      curr->emplace(elemkey, var);
    } else {
      auto it = curr->find(elemkey);
      if (it != curr->end()) {
        // must be CustomData type
        value::Value &data = it->second.get_raw_value();
        CustomDataType *p = data.as<CustomDataType>();
        if (p) {
          curr = p;
        } else {
          DCOUT("value is not dictionary");
          return false;
        }
      } else {
        // Add empty dictionary.
        CustomDataType customData;
        curr->emplace(elemkey, customData);
        DCOUT("add dict " << elemkey);

        MetaVariable &child = curr->at(elemkey);
        value::Value &data = child.get_raw_value();
        CustomDataType *childp = data.as<CustomDataType>();
        if (!childp) {
          DCOUT("childp is null");
          return false;
        }

        DCOUT("child = " << print_customData(*childp, "child", uint32_t(i)));

        // renew curr
        curr = childp;
      }
    }
  }

  DCOUT("dict = " << print_customData(custom, "custom", 0));


  return true;
}

bool HasCustomDataKey(const CustomDataType &custom, const std::string &key) {
  // split by namespace
  std::vector<std::string> names = split(key, ":");

  DCOUT(print_customData(custom, "customData", 0));

  if (names.empty()) {
    DCOUT("empty");
    return false;
  }

  if (names.size() > 1024) {
    DCOUT("too deep");
    // too deep
    return false;
  }

  const CustomDataType *curr = &custom;

  for (size_t i = 0; i < names.size(); i++) {
    const std::string &elemkey = names[i];
    DCOUT("elemkey = " << elemkey);

    DCOUT("dict = " << print_customData(*curr, "dict", uint32_t(i)));

    auto it = curr->find(elemkey);
    if (it == curr->end()) {
      DCOUT("key not found");
      return false;
    }

    if (i == (names.size() - 1)) {
      // leaf .ok
    } else {
      // must be CustomData type
      const value::Value &data = it->second.get_raw_value();
      const CustomDataType *p = data.as<CustomDataType>();
      if (p) {
        curr = p;
      } else {
        DCOUT("value is not dictionary type.");
        return false;
      }
    }
  }

  return true;
}

bool GetCustomDataByKey(const CustomDataType &custom, const std::string &key,
                        MetaVariable *var) {

  if (!var) {
    return false;
  }

  DCOUT(print_customData(custom, "customData", 0));

  // split by namespace
  std::vector<std::string> names = split(key, ":");

  if (names.empty()) {
    return false;
  }

  if (names.size() > 1024) {
    // too deep
    return false;
  }

  const CustomDataType *curr = &custom;

  for (size_t i = 0; i < names.size(); i++) {
    const std::string &elemkey = names[i];

    auto it = curr->find(elemkey);
    if (it == curr->end()) {
      return false;
    }

    if (i == (names.size() - 1)) {
      // leaf
      (*var) = it->second;
    } else {
      // must be CustomData type
      const value::Value &data = it->second.get_raw_value();
      const CustomDataType *p = data.as<CustomDataType>();
      if (p) {
        curr = p;
      } else {
        return false;
      }
    }
  }

  return true;
}

// TODO: Do test more.
// Current implementation may not behave as in pxrUSD's SdfPath's _LessThanInternal implementation
bool Path::LessThan(const Path &lhs, const Path &rhs) {
  DCOUT("LessThan");
  if (lhs.is_valid() && rhs.is_valid()) {
    // ok
  } else {
    DCOUT("invalid");
    return false;
  }

  // TODO: handle relative path correctly.
  if (lhs.is_absolute_path() && rhs.is_absolute_path()) {
    // ok
  } else {
    DCOUT("not absolute path");
    return false;
  }


  const std::string &lhs_prop_part = lhs.prop_part();
  const std::string &rhs_prop_part = rhs.prop_part();

  const std::vector<std::string> lhs_prim_names = split(lhs.prim_part(), "/");
  const std::vector<std::string> rhs_prim_names = split(rhs.prim_part(), "/");
  DCOUT("lhs_names = " << to_string(lhs_prim_names));
  DCOUT("rhs_names = " << to_string(rhs_prim_names));

  if (lhs_prim_names.size() < rhs_prim_names.size()) {
    return true;
  } else if (lhs_prim_names.size() == rhs_prim_names.size()) {
    // continue
  } else {
    DCOUT("greater");
    return false;
  }

  for (size_t i = 0; i < lhs_prim_names.size(); i++) {
    DCOUT(fmt::format("{}/{} compare = {}", i, lhs_prim_names.size(), lhs_prim_names[i].compare(rhs_prim_names[i])));
    if (lhs_prim_names[i].compare(rhs_prim_names[i]) < 0) {
      return true;
    } else if (lhs_prim_names[i].compare(rhs_prim_names[i]) > 0) {
      return false;
    } else {
      // equal. continue check.
    }
  }

  // prim path is equal.
  if (lhs_prop_part.empty() && rhs_prop_part.empty()) {
    // no prop part
    return false;
  }

  if (lhs_prop_part.empty()) {
    DCOUT("rhs has prim part.");
    return true;
  }

  DCOUT("prop compare." << lhs_prop_part.compare(rhs_prop_part));
  return (lhs_prop_part.compare(rhs_prop_part) < 0);
}

}  // namespace tinyusdz
