// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Common utilities for primitive reconstruction - Implementation

#include "reconstruct-common.hh"
#include "str-util.hh"
#include "common-macros.inc"

namespace tinyusdz {
namespace prim {

template<typename T>
nonstd::optional<Animatable<T>> ConvertToAnimatable(const primvar::PrimVar &var)
{
  Animatable<T> dst;

  if (!var.is_valid()) {
    DCOUT("is_valid failed");
    DCOUT("has_value " << var.has_value());
    DCOUT("has_timesamples " << var.has_timesamples());
    return nonstd::nullopt;
  }

  bool ok = false;

  if (var.has_value()) {
    if (auto pv = var.get_value<T>()) {
      dst.set_default(pv.value());
      ok = true;
    }
  }

  if (var.has_timesamples()) {
    for (size_t i = 0; i < var.ts_raw().size(); i++) {
      const value::TimeSamples::Sample &s = var.ts_raw().get_samples()[i];

      // Attribute Block?
      if (s.blocked || s.value.is_none()) {
        dst.add_blocked_sample(s.t);
      } else if (auto pv = s.value.get_value<T>()) {
        dst.add_sample(s.t, pv.value());
      } else {
        // Type mismatch
        DCOUT(i << "/" << var.ts_raw().size() << " type mismatch. expected " << value::TypeTraits<T>::type_name() << ", but got " << s.value.type_name());
        return nonstd::nullopt;
      }
    }
    ok = true;
  }

  if (ok) {
    return std::move(dst);
  }

  DCOUT("???");
  return nonstd::nullopt;
}

// Require special treatment for Extent(float3[2])
template<>
nonstd::optional<Animatable<Extent>> ConvertToAnimatable(const primvar::PrimVar &var)
{
  Animatable<Extent> dst;

  if (!var.is_valid()) {
    DCOUT("is_valid failed");
    return nonstd::nullopt;
  }

  bool value_ok = false;

  if (var.has_default()) {
    if (auto pv = var.get_value<std::vector<value::float3>>()) {
      if (pv.value().size() == 2) {
        Extent ext;
        ext.lower = pv.value()[0];
        ext.upper = pv.value()[1];
        dst.set_default(ext);
      } else {
        return nonstd::nullopt;
      }
    }
    value_ok = true;
  }

  if (var.has_timesamples()) {
    for (size_t i = 0; i < var.ts_raw().size(); i++) {
      const value::TimeSamples::Sample &s = var.ts_raw().get_samples()[i];

      // Attribute Block?
      if (s.blocked || s.value.is_none()) {
        dst.add_blocked_sample(s.t);
      } else if (auto pv = s.value.get_value<std::vector<value::float3>>()) {
        if (pv.value().size() == 2) {
          Extent ext;
          ext.lower = pv.value()[0];
          ext.upper = pv.value()[1];
          dst.add_sample(s.t, ext);
        } else {
          DCOUT(i << "/" << var.ts_raw().size() << " array size mismatch.");
          return nonstd::nullopt;
        }
      } else {
        // Type mismatch
        DCOUT(i << "/" << var.ts_raw().size() << " type mismatch.");
        return nonstd::nullopt;
      }
    }
    value_ok = true;
  }

  if (value_ok) {
    return std::move(dst);
  }

  DCOUT("???");
  return nonstd::nullopt;
}

// Explicit instantiations for common types
template nonstd::optional<Animatable<float>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<double>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<int>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::float2>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::float3>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::float4>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::color3f>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::normal3f>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::vector3f>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::point3f>> ConvertToAnimatable(const primvar::PrimVar &var);
template nonstd::optional<Animatable<value::matrix4d>> ConvertToAnimatable(const primvar::PrimVar &var);

// Enum handlers implementation
nonstd::expected<Axis, std::string> AxisEnumHandler(const std::string &tok) {
  using EnumTy = std::pair<Axis, const char *>;
  const std::vector<EnumTy> enums = {
      std::make_pair(Axis::X, "X"),
      std::make_pair(Axis::Y, "Y"),
      std::make_pair(Axis::Z, "Z"),
  };
  return EnumHandler<Axis>("axis", tok, enums);
}

nonstd::expected<Visibility, std::string> VisibilityEnumHandler(const std::string &tok) {
  using EnumTy = std::pair<Visibility, const char *>;
  const std::vector<EnumTy> enums = {
      std::make_pair(Visibility::Inherited, "inherited"),
      std::make_pair(Visibility::Invisible, "invisible"),
  };
  return EnumHandler<Visibility>(kVisibility, tok, enums);
}

nonstd::expected<Purpose, std::string> PurposeEnumHandler(const std::string &tok) {
  using EnumTy = std::pair<Purpose, const char *>;
  const std::vector<EnumTy> enums = {
      std::make_pair(Purpose::Default, "default"),
      std::make_pair(Purpose::Proxy, "proxy"),
      std::make_pair(Purpose::Render, "render"),
      std::make_pair(Purpose::Guide, "guide"),
  };
  return EnumHandler<Purpose>("purpose", tok, enums);
}

nonstd::expected<Orientation, std::string> OrientationEnumHandler(const std::string &tok) {
  using EnumTy = std::pair<Orientation, const char *>;
  const std::vector<EnumTy> enums = {
      std::make_pair(Orientation::RightHanded, "rightHanded"),
      std::make_pair(Orientation::LeftHanded, "leftHanded"),
  };
  return EnumHandler<Orientation>("orientation", tok, enums);
}

template <typename T>
nonstd::expected<T, std::string> EnumHandler(
    const std::string &prop_name, const std::string &tok,
    const std::vector<std::pair<T, const char *>> &enums) {
  auto ret = CheckAllowedTokens<T>(enums, tok);
  if (!ret) {
    return nonstd::make_unexpected(ret.error());
  }

  for (auto &item : enums) {
    if (tok == item.second) {
      return item.first;
    }
  }
  // Should never reach here, though.
  return nonstd::make_unexpected(
      quote(tok) + " is an invalid token for attribute `" + prop_name + "`");
}

template <class E>
nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::vector<std::pair<E, const char *>> &allowedTokens,
    const std::string &tok) {
  if (allowedTokens.empty()) {
    return true;
  }

  for (size_t i = 0; i < allowedTokens.size(); i++) {
    if (tok.compare(std::get<1>(allowedTokens[i])) == 0) {
      return true;
    }
  }

  std::vector<std::string> toks;
  for (size_t i = 0; i < allowedTokens.size(); i++) {
    toks.push_back(std::get<1>(allowedTokens[i]));
  }

  std::string s = join(", ", quote(toks));

  return nonstd::make_unexpected("Allowed tokens are [" + s + "] but got " +
                                 quote(tok) + ".");
}

// Note: The full implementation of ParseTypedAttribute and other template functions
// will remain in prim-reconstruct.cc for now to avoid breaking the build.
// They can be moved here in a follow-up refactoring.

} // namespace prim
} // namespace tinyusdz