// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// path.hh - Path class for USD scene paths
//
#pragma once

#include <string>
#include <utility>

#include "nonstd/optional.hpp"
#include "value-types.hh"

namespace tinyusdz {

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
  Path(Path &&rhs) noexcept = default;

  Path &operator=(const Path &rhs) = default;
  Path &operator=(Path &&rhs) noexcept = default;

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

  ///
  /// Estimate memory usage of this Path in bytes
  ///
  size_t estimate_memory_usage() const {
    size_t total = sizeof(Path);
    total += _prim_part.capacity();
    total += _prop_part.capacity();
    total += _variant_part.capacity();
    total += _variant_selection_part.capacity();
    total += _variant_part_str.capacity();
    total += _element.capacity();
    return total;
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

bool operator==(const Path &lhs, const Path &rhs);

namespace value {

#include "define-type-trait.inc"

DEFINE_TYPE_TRAIT(Path, "Path", TYPE_ID_PATH, 1);
// TODO(syoyo): Define as 1D array?
DEFINE_TYPE_TRAIT(std::vector<Path>, "PathVector", TYPE_ID_PATH_VECTOR, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
