// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
#include <algorithm>
#include <limits>
//
#include "prim-types.hh"
#include "str-util.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/pystring.h"

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
  Path p = (*this);

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
  Path p = (*this);

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

bool MetaVariable::IsObject() const {
  return (value.type_id() ==
          value::TypeTraits<tinyusdz::CustomDataType>::type_id);
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

bool ValidatePrimName(const std::string &name)
{
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


}  // namespace tinyusdz
