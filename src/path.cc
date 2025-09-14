// SPDX-License-Identifier: Apache 2.0
// Path class implementation

#include "path.hh"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

#include "str-util.hh"
#include "common-macros.inc"

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


// -- Path
//

void Path::_update(const std::string &p, const std::string &prop) {
  //
  // For absolute path, starts with '/' and no other '/' exists.
  // For property part, '.' exists only once.
  //

  if (p.empty() && prop.empty()) {
    _valid = false;
    return;
  }

  auto slash_fun = [](const char c) { return c == '/'; };
  auto dot_fun = [](const char c) { return c == '.'; };

  std::vector<std::string> prims = split(p, "/");

  // TODO: More checks('{', '[', ...)

  if (prop.size()) {
    // prop should not contain slashes
    auto nslashes = std::count_if(prop.begin(), prop.end(), slash_fun);
    if (nslashes) {
      _valid = false;
      return;
    }

    // prop does not start with '.'
    if (startsWith(prop, ".")) {
      _valid = false;
      return;
    }
  }

  if (p[0] == '/') {
    // absolute path

    auto ndots = std::count_if(p.begin(), p.end(), dot_fun);

    if (ndots == 0) {
      // absolute prim.
      _prim_part = p;

      if (prop.size()) {
        _prop_part = prop;
        _element = prop;
      } else {
        if (prims.size()) {
          _element = prims[prims.size() - 1];
        } else {
          _element = p;
        }
      }
      _valid = true;
    } else if (ndots == 1) {
      // prim_part contains property name.
      if (prop.size()) {
        // prop must be empty.
        _valid = false;
        return;
      }

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

      _prop_part = prop_name.erase(0, 1);  // remove '.'
      _prim_part = p.substr(0, size_t(loc));
      _element = _prop_part;  // elementName is property path

      _valid = true;

    } else {
      _valid = false;
      return;
    }

  } else if (p[0] == '.') {
    // maybe relative(e.g. "./xform", "../xform")
    // FIXME: Support relative path fully

#if 0
    auto nslashes = std::count_if(p.begin(), p.end(), slash_fun);
    if (nslashes > 0) {
      _valid = false;
      return;
    }

    _prop_part = p;
    _prop_part = _prop_part.erase(0, 1);
    _valid = true;
#else
    _prim_part = p;
    if (prop.size()) {
      _prop_part = prop;
      _element = prop;
    } else {
      if (prims.size()) {
        _element = prims[prims.size() - 1];
      } else {
        _element = p;
      }
    }
    _valid = true;

#endif

  } else {
    // prim.prop

    auto ndots = std::count_if(p.begin(), p.end(), dot_fun);
    if (ndots == 0) {
      // relative prim.
      _prim_part = p;
      if (prop.size()) {
        _prop_part = prop;
      }
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

Path::Path(const std::string &p, const std::string &prop) {
  _update(p, prop); 
}

Path Path::append_property(const std::string &elem) {
  Path &p = (*this);

  if (elem.empty()) {
    p._valid = false;
    return p;
  }

  if (is_variantElementName(elem)) {
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

const Path Path::AppendElement(const std::string &elem) const {
  Path p = (*this);  // copies

  p.append_element(elem);

  return p;
}

const Path Path::AppendProperty(const std::string &elem) const {
  Path p = (*this);  // copies

  p.append_property(elem);

  return p;
}

bool Path::replace_prefix(const Path &srcPrefix, const Path &dstPrefix) {
  const std::string &srcPrefixStr = srcPrefix.prim_part();
  const std::string &dstPrefixStr = dstPrefix.prim_part();

  std::string pathStr = prim_part();
  if (startsWith(pathStr, srcPrefixStr)) {
    pathStr = dstPrefixStr + removePrefix(pathStr, srcPrefixStr);

    _update(pathStr, prop_part());

    return true;
  }

  return false;
}

// TODO: Do test more.
// Current implementation may not behave as in pxrUSD's SdfPath's
// _LessThanInternal implementation
bool Path::LessThan(const Path &lhs, const Path &rhs) {
  // DCOUT("LessThan");
  if (lhs.is_valid() && rhs.is_valid()) {
    // ok
  } else {
    // Even though this should not happen,
    // valid paths is less than invalid paths
    return lhs.is_valid();
  }

  // TODO: handle relative path correctly.
  if (lhs.is_absolute_path() && rhs.is_absolute_path()) {
    // ok
  } else {
    // Absolute paths are less than relative paths
    return lhs.is_absolute_path();
  }

  if (lhs.prim_part() == rhs.prim_part()) {
    // compare property
    const std::string &lhs_prop_part = lhs.prop_part();
    const std::string &rhs_prop_part = rhs.prop_part();

    if (lhs_prop_part.empty() || rhs_prop_part.empty()) {
      return lhs_prop_part.empty();
    }

    return ::tinyusdz::lexicographical_compare(
        lhs_prop_part.begin(), lhs_prop_part.end(), rhs_prop_part.begin(),
        rhs_prop_part.end());

  } else {
    const std::vector<std::string> lhs_prim_names = split(lhs.prim_part(), "/");
    const std::vector<std::string> rhs_prim_names = split(rhs.prim_part(), "/");
    // DCOUT("lhs_names = " << to_string(lhs_prim_names));
    // DCOUT("rhs_names = " << to_string(rhs_prim_names));

    if (lhs_prim_names.empty() || rhs_prim_names.empty()) {
      return lhs_prim_names.empty() && rhs_prim_names.size();
    }

    // common shortest depth.
    size_t didx = (std::min)(lhs_prim_names.size(), rhs_prim_names.size());

    bool same_until_common_depth = true;
    for (size_t i = 0; i < didx; i++) {
      if (lhs_prim_names[i] != rhs_prim_names[i]) {
        same_until_common_depth = false;
        break;
      }
    }

    if (same_until_common_depth) {
      // tail differs. compare by depth count.
      return lhs_prim_names.size() < rhs_prim_names.size();
    }

    // Walk until common ancestor is found
    size_t child_idx = didx - 1;
    // DCOUT("common_depth_idx = " << didx << ", lcount = " <<
    // lhs_prim_names.size() << ", rcount = " << rhs_prim_names.size());
    if (didx > 1) {
      for (size_t parent_idx = didx - 2; parent_idx > 0; parent_idx--) {
        // DCOUT("parent_idx = " << parent_idx);
        if (lhs_prim_names[parent_idx] != rhs_prim_names[parent_idx]) {
          child_idx--;
        }
      }
    }
    // DCOUT("child_idx = " << child_idx);

    // compare child node
    return ::tinyusdz::lexicographical_compare(
        lhs_prim_names[child_idx].begin(), lhs_prim_names[child_idx].end(),
        rhs_prim_names[child_idx].begin(), rhs_prim_names[child_idx].end());
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

bool Path::has_prefix(const Path &prefix) const {
  if (!is_valid() || !prefix.is_valid()) {
    return false;
  }

  if (prefix.is_prim_property_path()) {
    // No hierarchy in Prim's property path, so use ==.
    return full_path_name() == prefix.full_path_name();
  } else if (prefix.is_prim_path()) {
    // '/', prefix = '/'
    if (is_root_path() && prefix.is_root_path()) {
      // DCOUT("both are root path");
      return true;
    }

    // example:
    // - '/bora', prefix = '/'
    // - '/bora/dora', prefix = '/'
    if (is_absolute_path() && prefix.is_root_path()) {
      // DCOUT("prefix is root path");
      return true;
    }

    const std::vector<std::string> prim_names = split(prim_part(), "/");
    const std::vector<std::string> prefix_prim_names =
        split(prefix.prim_part(), "/");
    // DCOUT("prim_names = " << to_string(prim_names));
    // DCOUT("prefix.prim_names = " << to_string(prefix_prim_names));

    if (prim_names.empty() || prefix_prim_names.empty()) {
      return false;
    }

    if (prim_names.size() < prefix_prim_names.size()) {
      return false;
    }

    size_t depth = prefix_prim_names.size();
    if (depth < 1) {  // just in case
      return false;
    }

    // Move to prefix's path depth and compare each elementName of Prim tree
    // towards the root. comapre from tail would find a difference earlier.
    while (depth > 0) {
      if (prim_names[depth - 1] != prefix_prim_names[depth - 1]) {
        return false;
      }
      depth--;
    }

    // DCOUT("has_prefix");
    return true;

  } else {
    // TODO: property-only path.
    DCOUT("TODO: Unsupported Path type in has_prefix()");
    return false;
  }
}

Path Path::append_element(const std::string &elem) {
  Path &p = (*this);

  if (elem.empty()) {
    p._valid = false;
    return p;
  }

  // {variant=value}
  if (is_variantElementName(elem)) {
    std::array<std::string, 2> variant;
    if (tokenize_variantElement(elem, &variant)) {
      _variant_part = variant[0];
      _variant_selection_part = variant[0];
      _prim_part += elem;
      _element = elem;
      return p;
    } else {
      p._valid = false;
    }
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

Path Path::get_parent_path() const {
  if (!_valid) {
    return Path();
  }

  if (is_root_path()) {
    Path p("", "");
    return p;
  }

  if (is_prim_property_path()) {
    // return prim part
    return Path(prim_part(), "");
  }

  size_t n = _prim_part.find_last_of('/');
  if (n == std::string::npos) {
    // relative path(e.g. "bora") or propery only path(e.g. ".myval").
    return Path();
  }

  if (n == 0) {
    // return root
    return Path("/", "");
  }

  return Path(_prim_part.substr(0, n), "");
}

Path Path::get_parent_prim_path() const {
  if (!_valid) {
    return Path();
  }

  if (is_root_prim()) {
    return *this;
  }

  if (is_prim_property_path()) {
    // return prim part
    return Path(prim_part(), "");
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

} // namespace tinyusdz