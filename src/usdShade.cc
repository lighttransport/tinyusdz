// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// UsdGeom API implementations

#include <sstream>

#include "usdShade.hh"
#include "str-util.hh"

#include "common-macros.inc"

namespace tinyusdz {

bool UsdShadePrim::has_sdr_metadata(const std::string &key) {
  if (!metas().sdrMetadata.has_value()) {
    return false;
  }

  const CustomDataType &dict = metas().sdrMetadata.value();

  if (!HasCustomDataKey(dict, key)) {
    return false;
  }

  // check the type of value.
  MetaVariable value;
  if (!GetCustomDataByKey(dict, key, &value)) {
    return false;
  }

  if (value.type_id() != value::TypeTraits<std::string>::type_id()) {
    return false;
  }

  return true;
}

const std::string UsdShadePrim::get_sdr_metadata(const std::string &key) {
  if (!metas().sdrMetadata.has_value()) {
    return std::string();
  }

  const CustomDataType &dict = metas().sdrMetadata.value();

  if (!HasCustomDataKey(dict, key)) {
    return std::string();
  }

  // check the type of value.
  MetaVariable var;
  if (!GetCustomDataByKey(dict, key, &var)) {
    return std::string();
  }

  if (var.type_id() != value::TypeTraits<std::string>::type_id()) {
    return std::string();
  }

  std::string svalue;
  if (!var.get_value(&svalue)) {
    return std::string();
  }

  return svalue;
}

bool UsdShadePrim::set_sdr_metadata(const std::string &key, const std::string &value) {

  CustomDataType &dict = metas().sdrMetadata.value();
  bool ret = SetCustomDataByKey(key, value, dict);
  return ret;
}

namespace {

//constexpr auto kPrimvarPrefix = "primvar::";

} // namespace


} // namespace tinyusdz


