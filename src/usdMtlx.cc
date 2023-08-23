// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#if defined(TINYUSDZ_USE_USDMTLX)

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/pugixml.hpp"
//#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif // TINYUSDZ_USE_USDMTLX

#include <sstream>

#include "usdMtlx.hh"

#if defined(TINYUSDZ_USE_USDMTLX)

#include "io-util.hh"
#include "tiny-format.hh"
#include "ascii-parser.hh" // To parse color3f value
#include "common-macros.inc"
#include "value-pprint.hh"
#include "pprinter.hh"
#include "tiny-format.hh"

#include "external/dtoa_milo.h"

inline std::string dtos(const double v) {

  char buf[128];
  dtoa_milo(v, buf);

  return std::string(buf);
}

#define PushError(msg) do { \
  if (err) { \
    (*err) += msg; \
  } \
} while(0);


namespace tinyusdz {

// defined in ascii-parser-base-types.cc
namespace ascii {

extern template bool AsciiParser::SepBy1BasicType<float>(const char sep, std::vector<float> *ret);

} // namespace ascii

namespace detail {

bool is_supported_type(const std::string &typeName);

bool is_supported_type(const std::string &typeName) {

  if (typeName.compare("integer") == 0) return true;
  if (typeName.compare("boolean") == 0) return true;
  if (typeName.compare("float") == 0) return true;
  if (typeName.compare("color3") == 0) return true;
  if (typeName.compare("color4") == 0) return true;
  if (typeName.compare("vector2") == 0) return true;
  if (typeName.compare("vector3") == 0) return true;
  if (typeName.compare("vector4") == 0) return true;
  if (typeName.compare("matrix33") == 0) return true;
  if (typeName.compare("matrix44") == 0) return true;
  if (typeName.compare("string") == 0) return true;
  if (typeName.compare("filename") == 0) return true;

  if (typeName.compare("integerarray") == 0) return true;
  if (typeName.compare("floatarray") == 0) return true;
  if (typeName.compare("vector2array") == 0) return true;
  if (typeName.compare("vector3array") == 0) return true;
  if (typeName.compare("vector4array") == 0) return true;
  if (typeName.compare("color3array") == 0) return true;
  if (typeName.compare("color4array") == 0) return true;
  if (typeName.compare("stringarray") == 0) return true;

  // No matrixarray

  // TODO
  //if (typeName.compare("color") == 0) return true;
  //if (typeName.compare("geomname") == 0) return true;
  //if (typeName.compare("geomnamearray") == 0) return true;

  return false;
}

template<typename T>
bool ParseValue(tinyusdz::ascii::AsciiParser &parser, T &ret, std::string *err);

template<>
bool ParseValue<int>(tinyusdz::ascii::AsciiParser &parser, int &ret, std::string *err) {

  int val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<int>::type_name()));
  }

  ret = val;

  return true;
}

template<>
bool ParseValue<bool>(tinyusdz::ascii::AsciiParser &parser, bool &ret, std::string *err) {

  bool val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<bool>::type_name()));
  }

  ret = val;

  return true;
}

template<>
bool ParseValue<float>(tinyusdz::ascii::AsciiParser &parser, float &ret, std::string *err) {

  float val;
  if (!parser.ReadBasicType(&val)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<float>::type_name()));
  }

  ret = val;

  return true;
}

template<>
bool ParseValue<value::float2>(tinyusdz::ascii::AsciiParser &parser, value::float2 &ret, std::string *err) {


  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<value::float3>::type_name()));
  }

  if (values.size() != 2) {
    PUSH_ERROR_AND_RETURN(fmt::format("type `{}` expects the number of elements is 2, but got {}",
       value::TypeTraits<value::float2>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];

  return true;
}

template<>
bool ParseValue<value::float3>(tinyusdz::ascii::AsciiParser &parser, value::float3 &ret, std::string *err) {


  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<value::float3>::type_name()));
  }

  if (values.size() != 3) {
    PUSH_ERROR_AND_RETURN(fmt::format("type `{}` expects the number of elements is 3, but got {}",
       value::TypeTraits<value::float3>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];

  return true;
}

template<>
bool ParseValue<value::float4>(tinyusdz::ascii::AsciiParser &parser, value::float4 &ret, std::string *err) {

  std::vector<float> values;
  if (!parser.SepBy1BasicType(',', &values)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to parse a value of type `{}`", value::TypeTraits<value::float4>::type_name()));
  }

  if (values.size() != 4) {
    PUSH_ERROR_AND_RETURN(fmt::format("type `{}` expects the number of elements is 4, but got {}",
       value::TypeTraits<value::float4>::type_name(), values.size()));
  }

  ret[0] = values[0];
  ret[1] = values[1];
  ret[2] = values[2];
  ret[3] = values[3];

  return true;
}

///
/// For MaterialX XML.
/// Parse string representation of Attribute value.
/// e.g. "0.0, 1.1" for vector2 type
/// NOTE: no parenthesis('(', '[') for vector and array type.
///
/// @param[in] typeName typeName(e.g. "vector2")
/// @param[in] str Ascii representation of value.
/// @param[out] value Ascii representation of value.
/// @param[out] err Parse error message when returning false.
///
///
/// Supported data type: boolean, float, color3, color4, vector2, vector3,
/// vector4, matrix33, matrix44, string, filename, integerarray, floatarray,
/// color3array, color4array, vector2array, vector3array, vector4array,
/// stringarray. Unsupported data type: geomname, geomnamearray
///
bool ParseMaterialXValue(const std::string &typeName, const std::string &str,
                           value::Value *value, std::string *err);

template<typename T> std::string to_xml_string(const T &val);

template<>
std::string to_xml_string(const float &val) {
  return dtos(double(val));
}

template<>
std::string to_xml_string(const int &val) {
  return std::to_string(val);
}

template<>
std::string to_xml_string(const value::color3f &val) {
  return dtos(double(val.r)) + ", " + dtos(double(val.g)) + ", " + dtos(double(val.b));
}

template<>
std::string to_xml_string(const value::normal3f &val) {
  return dtos(double(val.x)) + ", " + dtos(double(val.y)) + ", " + dtos(double(val.z));
}

template<typename T>
bool SerializeAttribute(const std::string &attr_name, const TypedAttributeWithFallback<Animatable<T>> &attr, std::string &value_str, std::string *err) {

  std::stringstream value_ss;

  if (attr.is_connection()) {
    PUSH_ERROR_AND_RETURN(fmt::format("TODO: connection attribute"));
  } else if (attr.is_blocked()) {
    // do nothing
    value_str = "";
    return true;
  } else {
    const Animatable<T> &animatable_value = attr.get_value();
    if (animatable_value.is_scalar()) {
      T value;
      if (animatable_value.get_scalar(&value)) {
        value_ss << "\"" << to_xml_string(value) << "\"";
      } else {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to get the value at default time of `{}`", attr_name));
      }
    } else if (animatable_value.is_timesamples()) {
      // no time-varying attribute in MaterialX. Use the value at default timecode.
      T value;
      if (animatable_value.get(value::TimeCode::Default(), &value)) {
        value_ss << "\"" << to_xml_string(value) << "\"";
      } else {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to get the value at default time of `{}`", attr_name));
      }
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get the value of `{}`", attr_name));
    }
  }

  value_str = value_ss.str();
  return true;
}

static bool WriteMaterialXToString(const MtlxUsdPreviewSurface &shader, std::string &xml_str, std::string *err) {
  // We directly write xml string for simplicity.
  //
  // TODO:
  // - [ ] Use pugixml to write xml string.

  std::stringstream ss;

  std::string node_name = "SR_default";

  ss << "<?xml version=\"1.0\"?>\n";
  // TODO: colorspace
  ss << "<materialx version=\"1.38\" colorspace=\"lin_rec709\">\n";
  ss << pprint::Indent(1) << "<UsdPreviewSurface name=\"" << node_name << "\" type =\"surfaceshader\">\n";

#define EMIT_ATTRIBUTE(__name, __tyname, __attr) { \
    std::string value_str; \
    if (!SerializeAttribute(__name, __attr, value_str, err)) { \
      return false; \
    } \
    if (value_str.size()) { \
      ss << pprint::Indent(2) << "<input name=\"" << __name << "\" type=\"" << __tyname << "\" value=\"" << value_str << "\" />\n"; \
    } \
  }

  // TODO: Attribute Connection
  EMIT_ATTRIBUTE("diffuseColor", "color3", shader.diffuseColor)
  EMIT_ATTRIBUTE("emissiveColor", "color3", shader.emissiveColor)
  EMIT_ATTRIBUTE("useSpecularWorkflow", "integer", shader.useSpecularWorkflow)
  EMIT_ATTRIBUTE("specularColor", "color3", shader.specularColor)
  EMIT_ATTRIBUTE("metallic", "float", shader.metallic)
  EMIT_ATTRIBUTE("roughness", "float", shader.roughness)
  EMIT_ATTRIBUTE("clearcoat", "float", shader.clearcoat)
  EMIT_ATTRIBUTE("clearcoatRoughness", "float", shader.clearcoatRoughness)
  EMIT_ATTRIBUTE("opacity", "float", shader.opacity)
  EMIT_ATTRIBUTE("opacityThreshold", "float", shader.opacityThreshold)
  EMIT_ATTRIBUTE("ior", "float", shader.ior)
  EMIT_ATTRIBUTE("normal", "vector3", shader.normal)
  EMIT_ATTRIBUTE("displacement", "float", shader.displacement)
  EMIT_ATTRIBUTE("occlusion", "float", shader.occlusion)

  ss << pprint::Indent(1) << "</UsdPreviewSurface>\n";

  ss << pprint::Indent(1) << "<surfacematerial name=\"USD_Default\" type=\"material\">\n";
  ss << pprint::Indent(2) << "<input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"" << node_name << "\" />\n";
  ss << pprint::Indent(1) << "</surfacematerial>\n";

  ss << "</materialx>\n";

  xml_str = ss.str();

  return true;
}

bool ParseMaterialXValue(const std::string &typeName, const std::string &str,
                           value::Value *value, std::string *err) {

  (void)value;

  if (!is_supported_type(typeName)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Invalid/unsupported type: {}", typeName));
  }

  tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(str.data()), str.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  if (typeName.compare("integer") == 0) {
    int val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("boolean") == 0) {
    bool val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("vector2") == 0) {
    value::float2 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("vector3") == 0) {
    value::float3 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else if (typeName.compare("vector4") == 0) {
    value::float4 val;
    if (!ParseValue(parser, val, err)) {
      return false;
    }
  } else {
    PUSH_ERROR_AND_RETURN("TODO: " + typeName);
  }

  // TODO
  return false;
}


} // namespace detail


bool ReadMaterialXFromString(const std::string &str, const std::string &asset_path, MtlxModel *mtlx, std::string *err) {


  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_string(str.c_str());
  if (!result) {
    std::string msg(result.description());
    PUSH_ERROR_AND_RETURN("Failed to parse XML: " + msg);
  }

  pugi::xml_node root = doc.child("materialx");
  if (!root) {
    PUSH_ERROR_AND_RETURN("<materialx> tag not found: " + asset_path);
  }

  // Attributes for a <materialx> element:
  //
  // - [x] version(string, required)
  //   - TODO: validate version string
  // - [ ] cms(string, optional)
  // - [ ] cmsconfig(filename, optional)
  // - [ ] colorspace(string, optional)
  // - [ ] namespace(string, optional)

  pugi::xml_attribute ver_attr = root.attribute("version");
  if (!ver_attr) {
    PUSH_ERROR_AND_RETURN("version attribute not found in <materialx>:" + asset_path);
  }

  //value::Value v;
  //if (!detail::ParseMaterialXValue("float", ver_attr.as_string(), &v, err)) {
  //  return false;
  //}
  //DCOUT("version = " << ver_attr.as_string());


  pugi::xml_attribute cms_attr = root.attribute("cms");
  if (cms_attr) {
  }

  pugi::xml_attribute cmsconfig_attr = root.attribute("cms");
  if (cmsconfig_attr) {
  }
  pugi::xml_attribute colorspace_attr = root.attribute("colorspace");
  if (colorspace_attr) {
  }

  pugi::xml_attribute namespace_attr = root.attribute("namespace");
  if (namespace_attr) {
  }


  // TODO
  (void)mtlx;

  return true;
}

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver, const std::string &asset_path, MtlxModel *mtlx, std::string *err) {

  std::string filepath = resolver.resolve(asset_path);
  if (filepath.empty()) {
    PUSH_ERROR_AND_RETURN("Asset not found: " + asset_path);
  }

  // up to 16MB xml
  size_t max_bytes = 1024 * 1024 * 16;

  std::vector<uint8_t> data;
  if (!io::ReadWholeFile(&data, err, filepath, max_bytes, /* userdata */nullptr)) {
    PUSH_ERROR_AND_RETURN("Read file failed.");
  }

  std::string str(reinterpret_cast<const char *>(&data[0]), data.size());
  return ReadMaterialXFromString(str, asset_path, mtlx, err);
}

bool WriteMaterialXToString(const MtlxModel &mtlx, std::string &xml_str,
                             std::string *err) {

  if (auto usdps = mtlx.shader.as<MtlxUsdPreviewSurface>()) {
    return detail::WriteMaterialXToString(*usdps, xml_str, err);
  } else if (auto adskss = mtlx.shader.as<MtlxAutodeskStandardSurface>()) {
    // TODO
    PUSH_ERROR_AND_RETURN("TODO: AutodeskStandardSurface");
  } else {
    // TODO
    PUSH_ERROR_AND_RETURN("Unknown/unsupported shader: " << mtlx.shader_name);
  }

  return false;
}

//} // namespace usdMtlx
} // namespace tinyusdz

#else

namespace tinyusdz {

bool ReadMaterialXFromFile(const AssetResolutionResolver &resolver, const std::string &asset_path, MtlxModel *mtlx, std::string *err) {

  (void)resolver;
  (void)asset_path;
  (void)mtlx;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

bool WriteMaterialXToString(const MtlxModel &mtlx, std::string &xml_str,
                             std::string *err) {
  (void)mtlx;
  (void)xml_str;

  if (err) {
    (*err) += "MaterialX support is disabled in this build.\n";
  }
  return false;
}

} // namespace tinyusdz

#endif // TINYUSDZ_USE_USDMTLX


