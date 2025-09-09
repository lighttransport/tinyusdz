// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
#include "dictionary.hh"

#include <algorithm>
#include <vector>

#include "str-util.hh"
#include "value-pprint.hh"
#include "common-macros.inc"

namespace tinyusdz {

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
      (*curr)[elemkey] = var;
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

namespace {

bool OverrideCustomDataRec(uint32_t depth, CustomDataType &dst,
                           const CustomDataType &src, const bool override_existing) {
  if (depth > (1024 * 1024 * 128)) {
    // too deep
    return false;
  }

  for (const auto &item : src) {
    if (dst.count(item.first)) {
      if (override_existing) {
        CustomDataType *dst_dict =
            dst.at(item.first).get_raw_value().as<CustomDataType>();

        const value::Value &src_data = item.second.get_raw_value();
        const CustomDataType *src_dict = src_data.as<CustomDataType>();

        //
        // Recursively apply override op both types are dict.
        //
        if (src_dict && dst_dict) {
          // recursively override dict
          if (!OverrideCustomDataRec(depth + 1, (*dst_dict), (*src_dict), override_existing)) {
            return false;
          }

        } else {
          dst[item.first] = item.second;
        }
      }
    } else {
      // add dict value
      dst.emplace(item.first, item.second);
    }
  }

  return true;
}

}  // namespace

void OverrideDictionary(CustomDataType &dst, const CustomDataType &src, const bool override_existing) {
  OverrideCustomDataRec(0, dst, src, override_existing);
}

}  // namespace tinyusdz