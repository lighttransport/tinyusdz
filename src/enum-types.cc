// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.

#include "enum-types.hh"

#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace tinyusdz {

//
// SpecType conversions
//
std::string to_string(const SpecType spec_type) {
  switch (spec_type) {
    case SpecType::Unknown: return "Unknown";
    case SpecType::Attribute: return "Attribute";
    case SpecType::Connection: return "Connection";
    case SpecType::Expression: return "Expression";
    case SpecType::Mapper: return "Mapper";
    case SpecType::MapperArg: return "MapperArg";
    case SpecType::Prim: return "Prim";
    case SpecType::PseudoRoot: return "PseudoRoot";
    case SpecType::Relationship: return "Relationship";
    case SpecType::RelationshipTarget: return "RelationshipTarget";
    case SpecType::Variant: return "Variant";
    case SpecType::VariantSet: return "VariantSet";
    case SpecType::Invalid: return "Invalid";
  }
  return "[[Invalid SpecType]]";
}

bool from_string(const std::string &str, SpecType *spec_type) {
  if (!spec_type) return false;
  
  static const std::unordered_map<std::string, SpecType> map = {
    {"Unknown", SpecType::Unknown},
    {"Attribute", SpecType::Attribute},
    {"Connection", SpecType::Connection},
    {"Expression", SpecType::Expression},
    {"Mapper", SpecType::Mapper},
    {"MapperArg", SpecType::MapperArg},
    {"Prim", SpecType::Prim},
    {"PseudoRoot", SpecType::PseudoRoot},
    {"Relationship", SpecType::Relationship},
    {"RelationshipTarget", SpecType::RelationshipTarget},
    {"Variant", SpecType::Variant},
    {"VariantSet", SpecType::VariantSet},
    {"Invalid", SpecType::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *spec_type = it->second;
    return true;
  }
  return false;
}

//
// Orientation conversions
//
std::string to_string(const Orientation orientation) {
  switch (orientation) {
    case Orientation::RightHanded: return "rightHanded";
    case Orientation::LeftHanded: return "leftHanded";
    case Orientation::Invalid: return "Invalid";
  }
  return "[[Invalid Orientation]]";
}

bool from_string(const std::string &str, Orientation *orientation) {
  if (!orientation) return false;
  
  static const std::unordered_map<std::string, Orientation> map = {
    {"rightHanded", Orientation::RightHanded},
    {"leftHanded", Orientation::LeftHanded},
    {"Invalid", Orientation::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *orientation = it->second;
    return true;
  }
  return false;
}

//
// Visibility conversions
//
std::string to_string(const Visibility visibility) {
  switch (visibility) {
    case Visibility::Inherited: return "inherited";
    case Visibility::Invisible: return "invisible";
    case Visibility::Invalid: return "Invalid";
  }
  return "[[Invalid Visibility]]";
}

bool from_string(const std::string &str, Visibility *visibility) {
  if (!visibility) return false;
  
  static const std::unordered_map<std::string, Visibility> map = {
    {"inherited", Visibility::Inherited},
    {"invisible", Visibility::Invisible},
    {"Invalid", Visibility::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *visibility = it->second;
    return true;
  }
  return false;
}

//
// Purpose conversions
//
std::string to_string(const Purpose purpose) {
  switch (purpose) {
    case Purpose::Default: return "default";
    case Purpose::Render: return "render";
    case Purpose::Proxy: return "proxy";
    case Purpose::Guide: return "guide";
  }
  return "[[Invalid Purpose]]";
}

bool from_string(const std::string &str, Purpose *purpose) {
  if (!purpose) return false;
  
  static const std::unordered_map<std::string, Purpose> map = {
    {"default", Purpose::Default},
    {"render", Purpose::Render},
    {"proxy", Purpose::Proxy},
    {"guide", Purpose::Guide},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *purpose = it->second;
    return true;
  }
  return false;
}

//
// Kind conversions
//
std::string to_string(const Kind kind) {
  switch (kind) {
    case Kind::Model: return "model";
    case Kind::Group: return "group";
    case Kind::Assembly: return "assembly";
    case Kind::Component: return "component";
    case Kind::Subcomponent: return "subcomponent";
    case Kind::SceneLibrary: return "sceneLibrary";
    case Kind::UserDef: return "userDef";
    case Kind::Invalid: return "Invalid";
  }
  return "[[Invalid Kind]]";
}

bool from_string(const std::string &str, Kind *kind) {
  if (!kind) return false;
  
  static const std::unordered_map<std::string, Kind> map = {
    {"model", Kind::Model},
    {"group", Kind::Group},
    {"assembly", Kind::Assembly},
    {"component", Kind::Component},
    {"subcomponent", Kind::Subcomponent},
    {"sceneLibrary", Kind::SceneLibrary},
    {"userDef", Kind::UserDef},
    {"Invalid", Kind::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *kind = it->second;
    return true;
  }
  return false;
}

//
// Interpolation conversions
//
std::string to_string(const Interpolation interpolation) {
  switch (interpolation) {
    case Interpolation::Constant: return "constant";
    case Interpolation::Uniform: return "uniform";
    case Interpolation::Varying: return "varying";
    case Interpolation::Vertex: return "vertex";
    case Interpolation::FaceVarying: return "faceVarying";
    case Interpolation::Invalid: return "Invalid";
  }
  return "[[Invalid Interpolation]]";
}

bool from_string(const std::string &str, Interpolation *interpolation) {
  if (!interpolation) return false;
  
  static const std::unordered_map<std::string, Interpolation> map = {
    {"constant", Interpolation::Constant},
    {"uniform", Interpolation::Uniform},
    {"varying", Interpolation::Varying},
    {"vertex", Interpolation::Vertex},
    {"faceVarying", Interpolation::FaceVarying},
    {"Invalid", Interpolation::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *interpolation = it->second;
    return true;
  }
  return false;
}

//
// ListEditQual conversions
//
std::string to_string(const ListEditQual list_edit_qual) {
  switch (list_edit_qual) {
    case ListEditQual::ResetToExplicit: return "unqualified";
    case ListEditQual::Append: return "append";
    case ListEditQual::Add: return "add";
    case ListEditQual::Delete: return "delete";
    case ListEditQual::Prepend: return "prepend";
    case ListEditQual::Order: return "order";
    case ListEditQual::Invalid: return "Invalid";
  }
  return "[[Invalid ListEditQual]]";
}

bool from_string(const std::string &str, ListEditQual *list_edit_qual) {
  if (!list_edit_qual) return false;
  
  static const std::unordered_map<std::string, ListEditQual> map = {
    {"unqualified", ListEditQual::ResetToExplicit},
    {"append", ListEditQual::Append},
    {"add", ListEditQual::Add},
    {"delete", ListEditQual::Delete},
    {"prepend", ListEditQual::Prepend},
    {"order", ListEditQual::Order},
    {"Invalid", ListEditQual::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *list_edit_qual = it->second;
    return true;
  }
  return false;
}

//
// Axis conversions
//
std::string to_string(const Axis axis) {
  switch (axis) {
    case Axis::X: return "X";
    case Axis::Y: return "Y";
    case Axis::Z: return "Z";
    case Axis::Invalid: return "Invalid";
  }
  return "[[Invalid Axis]]";
}

bool from_string(const std::string &str, Axis *axis) {
  if (!axis) return false;
  
  static const std::unordered_map<std::string, Axis> map = {
    {"X", Axis::X},
    {"Y", Axis::Y},
    {"Z", Axis::Z},
    {"x", Axis::X},  // Allow lowercase
    {"y", Axis::Y},
    {"z", Axis::Z},
    {"Invalid", Axis::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *axis = it->second;
    return true;
  }
  return false;
}

//
// Specifier conversions
//
std::string to_string(const Specifier specifier) {
  switch (specifier) {
    case Specifier::Def: return "def";
    case Specifier::Over: return "over";
    case Specifier::Class: return "class";
    case Specifier::Invalid: return "Invalid";
  }
  return "[[Invalid Specifier]]";
}

bool from_string(const std::string &str, Specifier *specifier) {
  if (!specifier) return false;
  
  static const std::unordered_map<std::string, Specifier> map = {
    {"def", Specifier::Def},
    {"over", Specifier::Over},
    {"class", Specifier::Class},
    {"Invalid", Specifier::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *specifier = it->second;
    return true;
  }
  return false;
}

//
// Permission conversions
//
std::string to_string(const Permission permission) {
  switch (permission) {
    case Permission::Public: return "public";
    case Permission::Private: return "private";
    case Permission::Invalid: return "Invalid";
  }
  return "[[Invalid Permission]]";
}

bool from_string(const std::string &str, Permission *permission) {
  if (!permission) return false;
  
  static const std::unordered_map<std::string, Permission> map = {
    {"public", Permission::Public},
    {"private", Permission::Private},
    {"Invalid", Permission::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *permission = it->second;
    return true;
  }
  return false;
}

//
// Variability conversions
//
std::string to_string(const Variability variability) {
  switch (variability) {
    case Variability::Varying: return "varying";
    case Variability::Uniform: return "uniform";
    case Variability::Config: return "config";
    case Variability::Invalid: return "Invalid";
  }
  return "[[Invalid Variability]]";
}

bool from_string(const std::string &str, Variability *variability) {
  if (!variability) return false;
  
  static const std::unordered_map<std::string, Variability> map = {
    {"varying", Variability::Varying},
    {"uniform", Variability::Uniform},
    {"config", Variability::Config},
    {"Invalid", Variability::Invalid},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *variability = it->second;
    return true;
  }
  return false;
}

//
// MaterialBindingStrength conversions
//
std::string to_string(const MaterialBindingStrength strength) {
  switch (strength) {
    case MaterialBindingStrength::WeakerThanDescendants: {
      return kWeaderThanDescendants;
    }
    case MaterialBindingStrength::StrongerThanDescendants: {
      return kStrongerThanDescendants;
    }
  }
  return "[[Invalid MaterialBindingStrength]]";
}

bool from_string(const std::string &str, MaterialBindingStrength *strength) {
  if (!strength) return false;
  
  static const std::unordered_map<std::string, MaterialBindingStrength> map = {
    {kWeaderThanDescendants, MaterialBindingStrength::WeakerThanDescendants},
    {kStrongerThanDescendants, MaterialBindingStrength::StrongerThanDescendants},
    {"weakerThanDescendants", MaterialBindingStrength::WeakerThanDescendants},
    {"strongerThanDescendants", MaterialBindingStrength::StrongerThanDescendants},
  };
  
  auto it = map.find(str);
  if (it != map.end()) {
    *strength = it->second;
    return true;
  }
  return false;
}

}  // namespace tinyusdz